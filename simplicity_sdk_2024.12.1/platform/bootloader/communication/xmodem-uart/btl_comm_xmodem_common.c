/***************************************************************************//**
 * @file
 * @brief Implements the communication core functionality
 *        for the UART XMODEM protocol
 *******************************************************************************
 * # License
 * <b>Copyright 2022 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc.  Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement.  This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

// -----------------------------------------------------------------------------
// Includes

#include "btl_comm_xmodem.h"
#include "btl_xmodem_config.h"
#include "driver/btl_serial_driver.h"
#include "driver/btl_driver_delay.h"

#include "core/flash/btl_internal_flash.h"

#if defined(BOOTLOADER_NONSECURE)
// NS headers
  #include "core/btl_reset_ns.h"
  #include "core/btl_bootload_ns.h"
#else
  #include "core/btl_reset.h"
  #include "core/btl_bootload.h"
#endif

// Debug
#include "debug/btl_debug.h"

#include <string.h>

// -----------------------------------------------------------------------------
// Defines

#ifndef BTL_XMODEM_IDLE_TIMEOUT
#error  "BTL_XMODEM_IDLE_TIMEOUT undefined."
#endif

// -----------------------------------------------------------------------------
// Static consts

static const char transferInitStr[] = "\r\nbegin upload\r\n";
static const char transferCompleteStr[] = "\r\nSerial upload complete\r\n";
static const char transferAbortedStr[] = "\r\nSerial upload aborted\r\n";
static const char xmodemError[] = "\r\nblock error 0x";
static const char fileError[] = "\r\nfile error 0x";
static const char bootError[] = "\r\nFailed to boot\r\n";

// -----------------------------------------------------------------------------
// Static local functions

static int32_t sendPacket(uint8_t packet)
{
  int32_t ret;
  ret = uart_sendByte(packet);

  if (packet == XMODEM_CMD_CAN) {
    // If packet is CAN, send three times
    uart_sendByte(packet);
    uart_sendByte(packet);
  }

  return ret;
}

static int32_t receivePacket(XmodemPacket_t *packet)
{
  int32_t ret = BOOTLOADER_OK;
  size_t requestedBytes;
  size_t receivedBytes;
  uint8_t *buf = (uint8_t *)packet;

  // Wait for bytes to be available in RX buffer
  delay_milliseconds(3000, false);
  while (uart_getRxAvailableBytes() == 0) {
    if (delay_expired()) {
      return BOOTLOADER_ERROR_COMMUNICATION_ERROR;
    }
  }

  // Read the first byte
  requestedBytes = 1;
  uart_receiveBuffer(buf,
                     requestedBytes,
                     &receivedBytes,
                     true,
                     1000);

  if (packet->header != XMODEM_CMD_SOH) {
    // All packets except XMODEM_CMD_SOH are single-byte
    return BOOTLOADER_OK;
  }

  requestedBytes = sizeof(XmodemPacket_t) - 1;
  ret = uart_receiveBuffer(buf + 1,
                           requestedBytes,
                           &receivedBytes,
                           true,
                           3000);

  if (receivedBytes != requestedBytes) {
    BTL_DEBUG_PRINT("Recvd ");
    BTL_DEBUG_PRINT_WORD_HEX(receivedBytes);
    BTL_DEBUG_PRINT_LF();
    // Didn't receive entire packet within 3000 ms; bail
    return BOOTLOADER_ERROR_COMMUNICATION_ERROR;
  }

  return ret;
}

static XmodemState_t getAction(bool confirm_erase)
{
  uint8_t c;
  XmodemState_t state;
  int ret = uart_receiveByteTimeout(&c, 1000UL);

  if (ret != BOOTLOADER_OK) {
    return IDLE;
  }

  switch (c) {
    case '1':
      state = INIT_TRANSFER;
      break;
    case '2':
      state = BOOT;
      break;
    case '3':
      state = MENU;
      break;
    case '4': {
      char str[] = "\r\nAre you sure? (y/n) > ";
      uart_sendBuffer((uint8_t *)str, sizeof(str), true);
      return CONFIRM_ERASE_NVM;
      break;
    }
    case 'y':
      if (confirm_erase) {
        return ERASE_NVM;
      }
      // Fall through
    default:
      state = MENU;
      break;
  }
  return state;
}

__STATIC_INLINE uint8_t nibbleToHex(uint8_t nibble)
{
  return (nibble > 9) ? (nibble - 10 + 'A') : (nibble + '0');
}

// -----------------------------------------------------------------------------
// Global Functions

void bootloader_xmodem_communication_init(void)
{
  uart_init();
}

int32_t bootloader_xmodem_communication_start(void)
{
  int32_t ret = BOOTLOADER_OK;
  char str[] = "\r\nGecko Bootloader vX.YY.ZZ\r\n"
               "1. upload gbl\r\n"
               "2. run\r\n"
               "3. ebl info\r\n"
               "4. erase nvm\r\n"
               "BL > ";

  uint32_t version = bootload_getBootloaderVersion();
  str[20] = nibbleToHex((version >> 24) & 0x0F);
  str[22] = (((version >> 16) & 0x0F) / 10 + '0');
  str[23] = (((version >> 16) & 0x0F) % 10 + '0');
  str[25] = ((version & 0x0F) / 10 + '0');
  str[26] = ((version & 0x0F) % 10 + '0');

  uart_sendBuffer((uint8_t *)str, sizeof(str), true);
  return ret;
}

int32_t bootloader_xmodem_communication_main(ImageProperties_t *imageProps,
                                             const BootloaderParserCallbacks_t* parseCb)
{
  int32_t ret = -1;

  XmodemState_t state = IDLE;
  XmodemReceiveBuffer_t buf;
  uint8_t response = 0;
  bool confirm_erase = false;
  int packetTimeout = 60;
#if BTL_XMODEM_IDLE_TIMEOUT > 0
  int idleTimeout = BTL_XMODEM_IDLE_TIMEOUT;
#endif

#if !defined(BOOTLOADER_NONSECURE)
  ParserContext_t parserContext = { 0 };
  DecryptContext_t decryptContext = { 0 };
  AuthContext_t authContext = { 0 };
#endif

  delay_init();
  while (1) {
    switch (state) {
      case MENU:
        // Print menu
        bootloader_xmodem_communication_start();
        BTL_DEBUG_PRINTLN("Menu");
        state = IDLE;
        break;

      case IDLE:
        // Get user input
        state = getAction(confirm_erase);

#if BTL_XMODEM_IDLE_TIMEOUT > 0
        if (state == IDLE) {
          idleTimeout--;
          if (idleTimeout == 0) {
            reset_resetWithReason(BOOTLOADER_RESET_REASON_TIMEOUT);
          }
        } else {
          idleTimeout = BTL_XMODEM_IDLE_TIMEOUT;
        }
#endif
        packetTimeout = 60;
        break;

      case INIT_TRANSFER:
        uart_sendBuffer((uint8_t *)transferInitStr,
                        sizeof(transferInitStr),
                        true);

        memset(imageProps, 0, sizeof(ImageProperties_t));
#if defined(BOOTLOADER_NONSECURE)
        parser_init(PARSER_FLAG_PARSE_CUSTOM_TAGS);
#else
        parser_init(&parserContext,
                    &decryptContext,
                    &authContext,
                    PARSER_FLAG_PARSE_CUSTOM_TAGS);
        imageProps->instructions = 0xFFU;
#endif
        imageProps->imageCompleted = false;
        imageProps->imageVerified = false;

        // Wait 5ms and see if we got any premature input; discard it
        delay_milliseconds(5, true);
        if (uart_getRxAvailableBytes()) {
          uart_flush(false, true);
        }

        // Initialize XMODEM parser
        xmodem_reset();

        state = WAIT_FOR_DATA;
        break;

      case WAIT_FOR_DATA:
        // Send 'C'
        sendPacket(XMODEM_CMD_C);
        delay_milliseconds(1000, false);
        while (uart_getRxAvailableBytes() == 0 && !delay_expired()) {
          // Do nothing
        }

        if (uart_getRxAvailableBytes()) {
          // We got a response; move to receive state
          state = RECEIVE_DATA;
        } else {
          // No response within 1 second; tick towards timeout
          packetTimeout--;
          if (packetTimeout == 0) {
            sendPacket(XMODEM_CMD_CAN);
            state = MENU;
          }
        }
        break;

      case RECEIVE_DATA:
        // Wait for a full XMODEM packet
        memset(&(buf.packet), 0, sizeof(XmodemPacket_t));
        ret = receivePacket(&(buf.packet));

        if (ret != BOOTLOADER_OK) {
          response = XMODEM_CMD_NAK;
          sendPacket(response);
          break;
        }

        ret = xmodem_parsePacket(&(buf.packet), &response);
        if (ret == BOOTLOADER_ERROR_XMODEM_DONE) {
          // XMODEM receive complete; return to menu
          state = COMPLETE;

          // Send CAN rather than ACK if the image verification failed
          if (!imageProps->imageCompleted || !imageProps->imageVerified) {
            BTL_DEBUG_PRINTLN("Checksum fail");
            response = XMODEM_CMD_CAN;
          }
        }

        if ((ret == BOOTLOADER_OK) && (buf.packet.header == XMODEM_CMD_SOH)) {
          // Packet is OK, parse contents
#if defined(BOOTLOADER_NONSECURE)
          (void)parseCb;
          ret = parser_parse(buf.packet.data,
                             XMODEM_DATA_SIZE,
                             imageProps);
#else
          ret = parser_parse(&parserContext,
                             imageProps,
                             buf.packet.data,
                             XMODEM_DATA_SIZE,
                             parseCb);
#endif
          if (ret != BOOTLOADER_OK) {
            // Parsing file failed; cancel transfer and return to menu
            BTL_DEBUG_PRINT("Parse fail ");
            BTL_DEBUG_PRINT_WORD_HEX(ret);
            BTL_DEBUG_PRINT_LF();
            response = XMODEM_CMD_CAN;
          }
        }

        if (response == XMODEM_CMD_CAN) {
          // Parsing packet failed; return to main menu
          state = COMPLETE;
        }

        // Send response
        sendPacket(response);
        break;

      case COMPLETE:
        BTL_DEBUG_PRINT("Complete ");
        BTL_DEBUG_PRINT_WORD_HEX(ret);
        BTL_DEBUG_PRINT_LF();
        uart_flush(false, true);

        delay_milliseconds(10, true);

        if ((response == XMODEM_CMD_ACK)
            && (ret == BOOTLOADER_ERROR_XMODEM_DONE)) {
          uart_sendBuffer((uint8_t *)transferCompleteStr,
                          sizeof(transferCompleteStr),
                          true);
        } else {
          uart_sendBuffer((uint8_t *)transferAbortedStr,
                          sizeof(transferAbortedStr),
                          true);

          if ((ret >= BOOTLOADER_ERROR_XMODEM_BASE)
              && (ret < BOOTLOADER_ERROR_PARSER_BASE)) {
            uart_sendBuffer((uint8_t *)xmodemError,
                            sizeof(xmodemError),
                            true);
          } else {
            uart_sendBuffer((uint8_t *)fileError,
                            sizeof(fileError),
                            true);
          }

          switch (ret) {
            case BOOTLOADER_ERROR_XMODEM_NO_SOH:
              response = 0x21;
              break;
            case BOOTLOADER_ERROR_XMODEM_PKTNUM:
              response = 0x22;
              break;
            case BOOTLOADER_ERROR_XMODEM_CRCL:
              response = 0x23;
              break;
            case BOOTLOADER_ERROR_XMODEM_CRCH:
              response = 0x24;
              break;
            case BOOTLOADER_ERROR_XMODEM_PKTSEQ:
              response = 0x25;
              break;
            case BOOTLOADER_ERROR_XMODEM_PKTDUP:
              response = 0x27;
              break;

            case BOOTLOADER_ERROR_PARSER_VERSION:
              response = 0x41; // BL_ERR_HEADER_EXP
              break;
            case BOOTLOADER_ERROR_PARSER_CRC:
              response = 0x43; // BL_ERR_CRC
              break;
            case BOOTLOADER_ERROR_PARSER_UNKNOWN_TAG:
              response = 0x44; // BL_ERR_UNKNOWN_TAG
              break;
            case BOOTLOADER_ERROR_PARSER_SIGNATURE:
              response = 0x45; // BL_ERR_SIG
              break;
            case BOOTLOADER_ERROR_PARSER_FILETYPE:
            case BOOTLOADER_ERROR_PARSER_UNEXPECTED:
            case BOOTLOADER_ERROR_PARSER_EOF:
              response = 0x4F; // BL_ERR_TAGBUF
              break;
            case BOOTLOADER_ERROR_PARSER_KEYERROR:
              response = 0x50; // BL_ERR_INV_KEY
              break;
          }

          uart_sendByte(nibbleToHex(response >> 4));
          uart_sendByte(nibbleToHex(response & 0x0F));
          uart_sendByte('\r');
          uart_sendByte('\n');
        }
        state = MENU;
        break;

      case BOOT:
        state = MENU;
        if (imageProps->imageCompleted && imageProps->imageVerified) {
#if defined(SEMAILBOX_PRESENT) || defined(CRYPTOACC_PRESENT)
          if (imageProps->contents & BTL_IMAGE_CONTENT_SE) {
            if (bootload_checkSeUpgradeVersion(imageProps->seUpgradeVersion)) {
              // Install SE upgrade
#if defined(BOOTLOADER_NONSECURE)
              bootload_commitSeUpgrade();
#else
              bootload_commitSeUpgrade(BTL_UPGRADE_LOCATION);
#endif
              // If we get here, the SE upgrade failed
            }
            // Return to menu
            break;
          }
#endif
          if (imageProps->contents & BTL_IMAGE_CONTENT_BOOTLOADER) {
            if (imageProps->bootloaderVersion > bootload_getBootloaderVersion()) {
              // Install bootloader upgrade
#if defined(BOOTLOADER_NONSECURE)
              bootload_commitBootloaderUpgrade(imageProps->bootloaderUpgradeSize);
#else
              bootload_commitBootloaderUpgrade(BTL_UPGRADE_LOCATION, imageProps->bootloaderUpgradeSize);
#endif
            }
          } else {
            // Enter app
            reset_resetWithReason(BOOTLOADER_RESET_REASON_GO);
          }
          // If we get here, the bootloader upgrade or reboot failed
          uart_sendBuffer((uint8_t *)bootError, sizeof(bootError), true);
        } else {
          // No upgrade image given, or upgrade failed
          reset_resetWithReason(BOOTLOADER_RESET_REASON_BADIMAGE);
        }
        break;
      
      case CONFIRM_ERASE_NVM:
        confirm_erase = true;
        state = IDLE;
        break;

      case ERASE_NVM:
        // Erase NVM
        // The address and size can be determined from the .map files after firmware compilation.
        // Right now, those are either:
        // - Controller 0x08074000, size 0xa000
        // - End device 0x08076000, size 0x8000
        // ...which both end at address 0x0807dfff
        uint32_t nvm_address = 0x08074000;
        uint32_t nvm_size = 0x0000a000;
        uint32_t zpal_page_size = 0x00002000;

        // In the page at address 0x0807e000, ZPAL stores tokens like the encryption key,
        // but also the QR code, DSK, etc. The controller firmware does not generate the DSK,
        // and the end device firmware only does it when the byte at address 0x807e45c is 0xff.

        // For some reason, we can only erase that page, but not unlock it for resetting that byte.
        // Hence we read the tokens first, erase NVM and the ZPAL page, then restore the tokens

        // It would be much easier to just have the controller firmware also generate a DSK,
        // but Silabs hides this functionality in the end device binaries...

        uint32_t btl_enc_key_address = 0x0807e284; // Actually at 0x0807e286, but that's not 4-byte aligned
        uint8_t btl_enc_key_data[20]; // Actually 16 bytes, but we need to read 20 bytes to keep the alignment
        memcpy(btl_enc_key_data, (uint8_t *)btl_enc_key_address, sizeof(btl_enc_key_data));

        uint32_t btl_sign_key_address = 0x0807e34c;
        uint8_t btl_sign_key_data[64];
        memcpy(btl_sign_key_data, (uint8_t *)btl_sign_key_address, sizeof(btl_sign_key_data));

        // Ideally, we'd preserve the public, private key and QR code across erases,
        // but the end device firmware also stores them (or a hash?) in the NVM
        // in a format I couldn't be arsed to reverse-engineer yet.
        // Until we do that, simply don't restore this information.
        // Otherwise, S2 inclusion simply stops working after the keys are confirmed.

        // uint32_t zpal_prk_address = 0x0807e3c0;
        // uint8_t zpal_prk_data[32];
        // memcpy(zpal_prk_data, (uint8_t *)zpal_prk_address, sizeof(zpal_prk_data));

        // uint32_t zpal_puk_address = 0x0807e3e0;
        // uint8_t zpal_puk_data[32];
        // memcpy(zpal_puk_data, (uint8_t *)zpal_puk_address, sizeof(zpal_puk_data));

        // uint32_t zpal_qr_address_1 = 0x0807e400;
        // uint32_t zpal_qr_address_2 = 0x0807e460;
        // uint8_t zpal_qr_data_1[92]; // actually 90, but that's not a multiple of 4
        // uint8_t zpal_qr_data_2[16];
        // memcpy(zpal_qr_data_1, (uint8_t *)zpal_qr_address_1, sizeof(zpal_qr_data_1));
        // memcpy(zpal_qr_data_2, (uint8_t *)zpal_qr_address_2, sizeof(zpal_qr_data_2));

        // Erase all pages that start inside the write range
        for (uint32_t pageAddress = nvm_address & ~(FLASH_PAGE_SIZE - 1UL);
            pageAddress < (nvm_address + nvm_size + zpal_page_size);
            pageAddress += FLASH_PAGE_SIZE) {
          flash_erasePage(pageAddress);
        }

        // Write the tokens back to where they belong
        flash_writeBuffer(btl_enc_key_address, btl_enc_key_data, sizeof(btl_enc_key_data));
        flash_writeBuffer(btl_sign_key_address, btl_sign_key_data, sizeof(btl_sign_key_data));
        // flash_writeBuffer(zpal_prk_address, zpal_prk_data, sizeof(zpal_prk_data));
        // flash_writeBuffer(zpal_puk_address, zpal_puk_data, sizeof(zpal_puk_data));
        // flash_writeBuffer(zpal_qr_address_1, zpal_qr_data_1, sizeof(zpal_qr_data_1));
        // flash_writeBuffer(zpal_qr_address_2, zpal_qr_data_2, sizeof(zpal_qr_data_2));

        // // To avoid re-initializing the QR code every time, also set the ready flag if we have a non-empty QR code
        // if (zpal_qr_data_1[0] != 0xff) {
        //   uint32_t qr_ready_address = 0x807e45c;
        //    // Needs to be 4-byte aligned
        //   uint8_t qr_ready_data[4] = {0, 0xff, 0xff, 0xff};
        //   flash_writeBuffer(qr_ready_address, qr_ready_data, sizeof(qr_ready_data));
        // }

        char str[] = "\r\nNVM erased\r\n";
        uart_sendBuffer((uint8_t *)str, sizeof(str), true);

        confirm_erase = false;
        state = MENU;
        break;
    }
  }
}
