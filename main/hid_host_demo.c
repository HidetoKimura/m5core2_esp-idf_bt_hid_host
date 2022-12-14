/*
 * Copyright (C) 2017 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define BTSTACK_FILE__ "hid_host_demo.c"

/*
 * hid_host_demo.c
 */

/* EXAMPLE_START(hid_host_demo): HID Host Demo
 *
 * @text This example implements an HID Host. For now, it connnects to a fixed device, queries the HID SDP
 * record and opens the HID Control + Interrupt channels
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>

#include <string.h>

#include "btstack_config.h"
#include "btstack.h"

#define MAX_ATTRIBUTE_VALUE_SIZE 300

#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
void hci_dump_open(const char *filename, hci_dump_format_t format);

// SDP Service Discovery Protocol
static uint8_t hid_descriptor[MAX_ATTRIBUTE_VALUE_SIZE];
static uint16_t hid_descriptor_len;

static uint16_t hid_control_psm;
static uint16_t hid_interrupt_psm;

static uint8_t attribute_value[MAX_ATTRIBUTE_VALUE_SIZE];
static const unsigned int attribute_value_buffer_size = MAX_ATTRIBUTE_VALUE_SIZE;

// L2CAP Logical Link Control and Adaptation Protocol
static uint16_t l2cap_hid_control_cid;
static uint16_t l2cap_hid_interrupt_cid;

// MBP 2016
// static const char * remote_addr_string = "F4-0F-24-3B-1B-E1";
// iMpulse static const char * remote_addr_string = "64:6E:6C:C1:AA:B5";
// 8bitdo
//static const char * remote_addr_string = "00:00:00:00:00:";
//static const char * remote_addr_string = "98:B6:E9:9B:16:6E"; // Switch mode
static const char * remote_addr_string = "E4:17:D8:64:16:6E"; // Keyboard mode

static bd_addr_t remote_addr;

static btstack_packet_callback_registration_t hci_event_callback_registration;

// Simplified US Keyboard with Shift modifier

#define CHAR_ILLEGAL     0xff
#define CHAR_RETURN     '\n'
#define CHAR_ESCAPE      27
#define CHAR_TAB         '\t'
#define CHAR_BACKSPACE   0x7f

#define MAX_DEVICES 20
enum DEVICE_STATE {
    REMOTE_NAME_REQUEST, REMOTE_NAME_INQUIRED, REMOTE_NAME_FETCHED
};
struct device {
    bd_addr_t address;
    uint8_t pageScanRepetitionMode;
    uint16_t clockOffset;
    enum DEVICE_STATE state;
};

#define INQUIRY_INTERVAL 5
struct device devices[MAX_DEVICES];
int deviceCount = 0;

/* @section Main application configuration
 *
 * @text In the application configuration, L2CAP is initialized 
 */

/* LISTING_START(PanuSetup): Panu setup */
static void packet_handler(uint8_t packet_type, uint16_t channel,
        uint8_t *packet, uint16_t size);
static void handle_sdp_client_query_result(uint8_t packet_type,
        uint16_t channel, uint8_t *packet, uint16_t size);

static void hid_host_setup(void) {

    // Initialize L2CAP 
    l2cap_init();

    // register for HCI events
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // Disable stdout buffering
    setbuf(stdout, NULL);
}

//extern void m5message(char* str);

/* LISTING_END */

/* @section SDP parser callback 
 * 
 * @text The SDP parsers retrieves the BNEP PAN UUID as explained in  
 * Section [on SDP BNEP Query example](#sec:sdpbnepqueryExample}.
 */

static void handle_sdp_client_query_result(uint8_t packet_type,
        uint16_t channel, uint8_t *packet, uint16_t size) {

    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    des_iterator_t attribute_list_it;
    des_iterator_t additional_des_it;
    des_iterator_t prot_it;
    uint8_t *des_element;
    uint8_t *element;
    uint32_t uuid;
    uint8_t status;
    switch (hci_event_packet_get_type(packet)) {
        case SDP_EVENT_QUERY_ATTRIBUTE_VALUE:
            if (sdp_event_query_attribute_byte_get_attribute_length(packet)
                    <= attribute_value_buffer_size) {
                attribute_value[sdp_event_query_attribute_byte_get_data_offset(
                        packet)] = sdp_event_query_attribute_byte_get_data(
                        packet);
                if ((uint16_t) (sdp_event_query_attribute_byte_get_data_offset(
                        packet) + 1)
                        == sdp_event_query_attribute_byte_get_attribute_length(
                                packet)) {
                    switch (sdp_event_query_attribute_byte_get_attribute_id(
                            packet)) {
                        case BLUETOOTH_ATTRIBUTE_PROTOCOL_DESCRIPTOR_LIST:
                            for (des_iterator_init(&attribute_list_it,
                                    attribute_value);
                                    des_iterator_has_more(&attribute_list_it);
                                    des_iterator_next(&attribute_list_it)) {
                                if (des_iterator_get_type(&attribute_list_it)
                                        != DE_DES)
                                    continue;
                                des_element = des_iterator_get_element(
                                        &attribute_list_it);
                                des_iterator_init(&prot_it, des_element);
                                element = des_iterator_get_element(&prot_it);
                                if (!element)
                                    continue;
                                if (de_get_element_type(element) != DE_UUID)
                                    continue;
                                uuid = de_get_uuid32(element);
                                des_iterator_next(&prot_it);
                                switch (uuid) {
                                    case BLUETOOTH_PROTOCOL_L2CAP:
                                        if (!des_iterator_has_more(&prot_it))
                                            continue;
                                        de_element_get_uint16(
                                                des_iterator_get_element(
                                                        &prot_it),
                                                &hid_control_psm);
                                        printf("HID Control PSM: 0x%04x\n",
                                                (int) hid_control_psm);
                                        break;
                                    default:
                                        break;
                                }
                            }
                            break;
                        case BLUETOOTH_ATTRIBUTE_ADDITIONAL_PROTOCOL_DESCRIPTOR_LISTS:
                            for (des_iterator_init(&attribute_list_it,
                                    attribute_value);
                                    des_iterator_has_more(&attribute_list_it);
                                    des_iterator_next(&attribute_list_it)) {
                                if (des_iterator_get_type(&attribute_list_it)
                                        != DE_DES)
                                    continue;
                                des_element = des_iterator_get_element(
                                        &attribute_list_it);
                                for (des_iterator_init(&additional_des_it,
                                        des_element);
                                        des_iterator_has_more(
                                                &additional_des_it);
                                        des_iterator_next(&additional_des_it)) {
                                    if (des_iterator_get_type(
                                            &additional_des_it) != DE_DES)
                                        continue;
                                    des_element = des_iterator_get_element(
                                            &additional_des_it);
                                    des_iterator_init(&prot_it, des_element);
                                    element = des_iterator_get_element(
                                            &prot_it);
                                    if (!element)
                                        continue;
                                    if (de_get_element_type(element) != DE_UUID)
                                        continue;
                                    uuid = de_get_uuid32(element);
                                    des_iterator_next(&prot_it);
                                    switch (uuid) {
                                        case BLUETOOTH_PROTOCOL_L2CAP:
                                            if (!des_iterator_has_more(
                                                    &prot_it))
                                                continue;
                                            de_element_get_uint16(
                                                    des_iterator_get_element(
                                                            &prot_it),
                                                    &hid_interrupt_psm);
                                            printf(
                                                    "HID Interrupt PSM: 0x%04x\n",
                                                    (int) hid_interrupt_psm);
                                            break;
                                        default:
                                            break;
                                    }
                                }
                            }
                            break;
                        case BLUETOOTH_ATTRIBUTE_HID_DESCRIPTOR_LIST:
                            for (des_iterator_init(&attribute_list_it,
                                    attribute_value);
                                    des_iterator_has_more(&attribute_list_it);
                                    des_iterator_next(&attribute_list_it)) {
                                if (des_iterator_get_type(&attribute_list_it)
                                        != DE_DES)
                                    continue;
                                des_element = des_iterator_get_element(
                                        &attribute_list_it);
                                for (des_iterator_init(&additional_des_it,
                                        des_element);
                                        des_iterator_has_more(
                                                &additional_des_it);
                                        des_iterator_next(&additional_des_it)) {
                                    if (des_iterator_get_type(
                                            &additional_des_it) != DE_STRING)
                                        continue;
                                    element = des_iterator_get_element(
                                            &additional_des_it);
                                    const uint8_t * descriptor = de_get_string(
                                            element);
                                    hid_descriptor_len = de_get_data_size(
                                            element);
                                    memcpy(hid_descriptor, descriptor,
                                            hid_descriptor_len);
                                    printf("HID Descriptor:\n");
                                    printf_hexdump(hid_descriptor,
                                            hid_descriptor_len);
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
            }
            else {
                fprintf(stderr,
                        "SDP attribute value buffer size exceeded: available %d, required %d\n",
                        attribute_value_buffer_size,
                        sdp_event_query_attribute_byte_get_attribute_length(
                                packet));
            }
            break;

        case SDP_EVENT_QUERY_COMPLETE:
            if (!hid_control_psm) {
                printf("HID Control PSM missing\n");
                //m5message("HID Device not found");
                break;
            }
            if (!hid_interrupt_psm) {
                printf("HID Interrupt PSM missing\n");
                //m5message("HID Device not found");
                break;
            }
            printf("Setup HID\n");
            status = l2cap_create_channel(packet_handler, remote_addr,
                    hid_control_psm, 48, &l2cap_hid_control_cid);
            if (status) {
                printf("Connecting to HID Control failed: 0x%02x\n", status);
            }
            break;
    }
}

extern void m5print(char* str);

enum STATE {
    INIT, ACTIVE, CONNECT
};
enum STATE state = INIT;

static btstack_packet_callback_registration_t hci_event_callback_registration;

static int getDeviceIndexForAddress(bd_addr_t addr) {
    int j;
    for (j = 0; j < deviceCount; j++) {
        if (bd_addr_cmp(addr, devices[j].address) == 0) {
            return j;
        }
    }
    return -1;
}

static void start_scan(void) {
    printf("Starting inquiry scan..\n");
    gap_inquiry_start(INQUIRY_INTERVAL);
}

static int has_more_remote_name_requests(void) {
    int i;
    for (i = 0; i < deviceCount; i++) {
        if (devices[i].state == REMOTE_NAME_REQUEST)
            return 1;
    }
    return 0;
}

static void do_next_remote_name_request(void) {
    int i;
    for (i = 0; i < deviceCount; i++) {
        // remote name request
        if (devices[i].state == REMOTE_NAME_REQUEST) {
            devices[i].state = REMOTE_NAME_INQUIRED;
            printf("Get remote name of %s...\n",
                    bd_addr_to_str(devices[i].address));
            gap_remote_name_request(devices[i].address,
                    devices[i].pageScanRepetitionMode,
                    devices[i].clockOffset | 0x8000);
            return;
        }
    }
}

static void continue_remote_names(void) {
    if (has_more_remote_name_requests()) {
        do_next_remote_name_request();
        return;
    }
    start_scan();
}

//extern void m5addDevice(char* addr, char* name);

//extern void m5packetReceive(const void *data, int size);

void connect(char* addr) {
    printf("Connect with %s \n", addr);
    state = CONNECT;
    gap_inquiry_stop();
    sscanf_bd_addr(addr, remote_addr);
    printf("Start SDP HID query for remote HID Device.\n");
    sdp_client_query_uuid16(&handle_sdp_client_query_result, remote_addr,
    BLUETOOTH_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE_SERVICE);

}

char * tempAddr;
/*
 * @section Packet Handler
 * 
 * @text The packet handler responds to various HCI Events.
 */

/* LISTING_START(packetHandler): Packet Handler */
static void packet_handler(uint8_t packet_type, uint16_t channel,
        uint8_t *packet, uint16_t size) {
    /* LISTING_PAUSE */
    uint8_t event = 0;
    bd_addr_t event_addr;
    uint8_t status;
    uint16_t l2cap_cid;

    bd_addr_t addr;
    int i;
    int index;
    switch (packet_type) {
        case HCI_EVENT_PACKET:
            event = hci_event_packet_get_type(packet);
            switch (state) {
                /* @text In INIT, an inquiry  scan is started, and the application transits to 
                 * ACTIVE state.
                 */
                case INIT:
                    switch (event) {
                        case BTSTACK_EVENT_STATE:
                            if (btstack_event_state_get_state(packet)
                                    == HCI_STATE_WORKING) {
                                start_scan();
                                state = ACTIVE;
                            }
                            break;
                        default:
                            break;
                    }
                    break;

                    /* @text In ACTIVE, the following events are processed:
                     *  - GAP Inquiry result event: BTstack provides a unified inquiry result that contain
                     *    Class of Device (CoD), page scan mode, clock offset. RSSI and name (from EIR) are optional.
                     *  - Inquiry complete event: the remote name is requested for devices without a fetched
                     *    name. The state of a remote name can be one of the following:
                     *    REMOTE_NAME_REQUEST, REMOTE_NAME_INQUIRED, or REMOTE_NAME_FETCHED.
                     *  - Remote name request complete event: the remote name is stored in the table and the
                     *    state is updated to REMOTE_NAME_FETCHED. The query of remote names is continued.
                     */
                case ACTIVE:
                    switch (event) {
                        case GAP_EVENT_INQUIRY_RESULT:
                            if (deviceCount >= MAX_DEVICES)
                                break;  // already full
                            gap_event_inquiry_result_get_bd_addr(packet, addr);
                            index = getDeviceIndexForAddress(addr);
                            if (index >= 0)
                                break;   // already in our list

                            memcpy(devices[deviceCount].address, addr, 6);
                            devices[deviceCount].pageScanRepetitionMode =
                                    gap_event_inquiry_result_get_page_scan_repetition_mode(
                                            packet);
                            devices[deviceCount].clockOffset =
                                    gap_event_inquiry_result_get_clock_offset(
                                            packet);
                            // print info
                            printf("Device found: %s ", bd_addr_to_str(addr));
                            printf("with COD: 0x%06x, ",
                                    (unsigned int) gap_event_inquiry_result_get_class_of_device(
                                            packet));
                            printf("pageScan %d, ",
                                    devices[deviceCount].pageScanRepetitionMode);
                            printf("clock offset 0x%04x",
                                    devices[deviceCount].clockOffset);
                            if (gap_event_inquiry_result_get_rssi_available(
                                    packet)) {
                                printf(", rssi %d dBm",
                                        (int8_t) gap_event_inquiry_result_get_rssi(
                                                packet));
                            }
                            if (gap_event_inquiry_result_get_name_available(
                                    packet)) {
                                char name_buffer[240];
                                int name_len =
                                        gap_event_inquiry_result_get_name_len(
                                                packet);
                                memcpy(name_buffer,
                                        gap_event_inquiry_result_get_name(
                                                packet), name_len);
                                name_buffer[name_len] = 0;
                                printf(", name '%s'", name_buffer);

                                //m5addDevice(bd_addr_to_str(addr), name_buffer);
                                if( 0 == strcmp(bd_addr_to_str(addr), remote_addr_string) ) {
                                    connect((char*)remote_addr_string);
                                }

                                devices[deviceCount].state =
                                        REMOTE_NAME_FETCHED;
                            }
                            else {
                                devices[deviceCount].state =
                                        REMOTE_NAME_REQUEST;
                                tempAddr = bd_addr_to_str(addr);
                            }
                            printf("\n");
                            deviceCount++;
                            break;

                        case GAP_EVENT_INQUIRY_COMPLETE:
                            for (i = 0; i < deviceCount; i++) {
                                // retry remote name request
                                if (devices[i].state == REMOTE_NAME_INQUIRED)
                                    devices[i].state = REMOTE_NAME_REQUEST;
                            }
                            continue_remote_names();
                            break;

                        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE:
                            reverse_bd_addr(&packet[3], addr);
                            index = getDeviceIndexForAddress(addr);
                            if (index >= 0) {
                                if (packet[2] == 0) {
                                    printf("Name: '%s'\n", &packet[9]);
                                    devices[index].state = REMOTE_NAME_FETCHED;

                                    //m5addDevice(tempAddr,
                                    //        (const char *) &packet[9]);
                                    if( 0 == strcmp(bd_addr_to_str(addr), remote_addr_string) ) {
                                        connect((char*)remote_addr_string);
                                    }

                                }
                                else {
                                    printf(
                                            "Failed to get name: page timeout\n");
                                }
                            }
                            continue_remote_names();
                            break;
                        default:
                            break;
                    }
                    break;
                case CONNECT:
                    switch (event) {

                        case BTSTACK_EVENT_STATE:
                            if (btstack_event_state_get_state(packet)
                                    == HCI_STATE_WORKING) {
                                printf(
                                        "Start SDP HID query for remote HID Device.\n");
                                sdp_client_query_uuid16(
                                        &handle_sdp_client_query_result,
                                        remote_addr,
                                        BLUETOOTH_SERVICE_CLASS_HUMAN_INTERFACE_DEVICE_SERVICE);
                            }
                            break;

                            /* LISTING_PAUSE */
                        case HCI_EVENT_PIN_CODE_REQUEST:
                            // inform about pin code request
                            printf("Pin code request - using '0000'\n");
                            hci_event_pin_code_request_get_bd_addr(packet,
                                    event_addr);
                            gap_pin_code_response(event_addr, "0000");
                            break;

                        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                            // inform about user confirmation request
                            printf(
                                    "SSP User Confirmation Request with numeric value '%"PRIu32"'\n",
                                    little_endian_read_32(packet, 8));
                            printf("SSP User Confirmation Auto accept\n");
                            break;

                            /* LISTING_RESUME */

                        case L2CAP_EVENT_CHANNEL_OPENED:
                            status = packet[2];
                            if (status) {
                                printf("L2CAP Connection failed: 0x%02x\n",
                                        status);
                                break;
                            }
                            l2cap_cid = little_endian_read_16(packet, 13);
                            if (!l2cap_cid)
                                break;
                            if (l2cap_cid == l2cap_hid_control_cid) {
                                status = l2cap_create_channel(packet_handler,
                                        remote_addr, hid_interrupt_psm, 48,
                                        &l2cap_hid_interrupt_cid);
                                if (status) {
                                    printf(
                                            "Connecting to HID Control failed: 0x%02x\n",
                                            status);
                                    break;
                                }
                            }
                            if (l2cap_cid == l2cap_hid_interrupt_cid) {
                                printf("HID Connection established\n");
                                //m5message("HID Device connected");
                            }
                            break;
                        default:
                            break;

                    }
                    break;
                default:
                    break;
            }
            break;
        case L2CAP_DATA_PACKET:
            printf_hexdump(packet, size);
            //m5packetReceive(packet, size);
            break;
        default:
            break;

    }
}

//extern void m5_arduino_main();

int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]) {

    (void) argc;
    (void) argv;

    hid_host_setup();

    // Turn on the device 
    hci_power_control(HCI_POWER_ON);

//    m5_arduino_main();

    return 0;
}