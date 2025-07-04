/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include "btstack.h"
#include "hardware/adc.h"

#include "mic.h"
#include "server_common.h"

#define APP_AD_FLAGS 0x06

extern bool blue_start;

static uint8_t adv_data[] = {
    // Flags general discoverable
    0x02,
    BLUETOOTH_DATA_TYPE_FLAGS,
    APP_AD_FLAGS,
    // Name
    0x17,
    BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'P',
    'i',
    'c',
    'o',
    ' ',
    '0',
    '0',
    ':',
    '0',
    '0',
    ':',
    '0',
    '0',
    ':',
    '0',
    '0',
    ':',
    '0',
    '0',
    ':',
    '0',
    '0',
    0x03,
    BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    0x1a,
    0x18,
};
static const uint8_t adv_data_len = sizeof(adv_data);

int le_notification_enabled;
hci_con_handle_t con_handle;
uint16_t current_temp;
const char *value = "hello";
extern int val;
extern bool recording;
char message[50];

void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(size);
    UNUSED(channel);
    bd_addr_t local_addr;
    if (packet_type != HCI_EVENT_PACKET)
        return;

    uint8_t event_type = hci_event_packet_get_type(packet);
    switch (event_type)
    {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) != HCI_STATE_WORKING)
            return;
        gap_local_bd_addr(local_addr);
        printf("BTstack up and running on %s.\n", bd_addr_to_str(local_addr));

        // setup advertisements
        uint16_t adv_int_min = 800;
        uint16_t adv_int_max = 800;
        uint8_t adv_type = 0;
        bd_addr_t null_addr;
        memset(null_addr, 0, 6);
        gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
        assert(adv_data_len <= 31); // ble limitation
        gap_advertisements_set_data(adv_data_len, (uint8_t *)adv_data);
        gap_advertisements_enable(1);

        // poll_temp();

        break;
    case HCI_EVENT_DISCONNECTION_COMPLETE:
        le_notification_enabled = 0;
        break;
    case ATT_EVENT_CAN_SEND_NOW:
        val;
        sprintf(message, "Nowe nagranie%d", val);
        att_server_notify(con_handle, ATT_CHARACTERISTIC_12345678_1234_5678_1234_56789abcdef1_01_VALUE_HANDLE, (const uint8_t *)message, strlen(message));
        break;
    default:
        break;
    }
}

uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    UNUSED(connection_handle);

    if (att_handle == ATT_CHARACTERISTIC_12345678_1234_5678_1234_56789abcdef1_01_VALUE_HANDLE) // ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_TEMPERATURE_01_VALUE_HANDLE)
    {

        return att_read_callback_handle_blob((const uint8_t *)value, strlen(value), offset, buffer, buffer_size);
        // return att_read_callback_handle_blob((const uint8_t *)&current_temp, sizeof(current_temp), offset, buffer, buffer_size);
    }
    return 0;
}

int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    UNUSED(transaction_mode);
    UNUSED(offset);
    UNUSED(buffer_size);
    if (att_handle == ATT_CHARACTERISTIC_12345678_1234_5678_1234_56789abcdef2_01_VALUE_HANDLE)
    {
        // Skopiuj dane do bufora i zakończ null-em
        static char received_data[100];
        size_t len = buffer_size > sizeof(received_data) - 1 ? sizeof(received_data) - 1 : buffer_size;
        memcpy(received_data, buffer, len);
        received_data[len] = 0;

        printf("Received over BLE: %s\n", received_data);
        char znak = *"S";
        char koniec = *"P";
        if (received_data[0] == znak)
        {
            blue_start = true;
        }
        if (received_data[0] == koniec)
        {
            recording = false;
        }
    }

    if (att_handle != ATT_CHARACTERISTIC_12345678_1234_5678_1234_56789abcdef1_01_CLIENT_CONFIGURATION_HANDLE)
        return 0;
    le_notification_enabled = little_endian_read_16(buffer, 0) == GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION;
    con_handle = connection_handle;
    /*if (le_notification_enabled)
    {
        att_server_request_can_send_now_event(con_handle);
    }*/

    return 0;
}
