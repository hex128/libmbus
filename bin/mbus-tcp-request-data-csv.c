//------------------------------------------------------------------------------
// Copyright (C) 2011, Robert Johansson, Raditex AB
// All rights reserved.
//
// rSCADA
// http://www.rSCADA.se
// info@rscada.se
//
//------------------------------------------------------------------------------

#include <string.h>

#include <stdio.h>
#include <math.h>
#include <mbus/mbus.h>

static int debug = 0;

//------------------------------------------------------------------------------
// Execution starts here:
//------------------------------------------------------------------------------
int
main(int argc, char **argv) {
    mbus_frame reply;
    mbus_frame_data reply_data;
    mbus_handle *handle = NULL;

    char *host, *addr_str;
    int address;
    long port;
    int max_tries = 1;
    int addr_c;
    int addr_base;

    memset((void *) &reply, 0, sizeof(mbus_frame));
    memset((void *) &reply_data, 0, sizeof(mbus_frame_data));

    if (argc >= 4) {
        int arg_pos = 1;
        if (strcmp(argv[arg_pos], "-d") == 0) {
            debug = 1;
            arg_pos++;
        }
        if (strcmp(argv[arg_pos], "-r") == 0) {
            arg_pos++;
            max_tries = atoi(argv[arg_pos]);
            arg_pos++;
        }
        host = argv[arg_pos];
        arg_pos++;
        port = atol(argv[arg_pos]);
        if (argc - arg_pos == 1) {
            addr_base = -1;
            addr_c = 250;
        } else {
            arg_pos++;
            int base = arg_pos;
            addr_c = argc - base;
            addr_base = base;
        }
    } else {
        fprintf(stderr, "usage: %s [-d] host port mbus-address\n", argv[0]);
        fprintf(stderr, "    optional flag -d for debug printout\n");
        return 0;
    }

    if ((port < 0) || (port > 0xFFFF)) {
        fprintf(stderr, "Invalid port: %ld\n", port);
        return 1;
    }

    if ((handle = mbus_context_tcp(host, port)) == NULL) {
        fprintf(stderr, "Could not initialize M-Bus context: %s\n", mbus_error_str());
        return 1;
    }

    if (debug) {
        mbus_register_send_event(handle, &mbus_dump_send_event);
        mbus_register_recv_event(handle, &mbus_dump_recv_event);
    }

    if (mbus_connect(handle) == -1) {
        fprintf(stderr, "Failed to setup connection to M-bus gateway\n");
        mbus_context_free(handle);
        return 1;
    }

    char seq_addr_str[4] = "1";
    uint8_t i = 0;
    int try_count = 0;

    while (i < addr_c) {
        try_count++;
        if (try_count > max_tries) {
            printf("%s,,,,\n", addr_str);
            i++;
            try_count = 0;
        }
        if (i >= addr_c) {
            break;
        }
        if (addr_base == -1) {
            sprintf(seq_addr_str, "%d", i + 1);
            addr_str = seq_addr_str;
        } else {
            addr_str = argv[addr_base + i];
        }
        fprintf(stderr, "Reading address %s\n", addr_str);

        if (mbus_is_secondary_address(addr_str)) {
            // secondary addressing

            int ret;

            ret = mbus_select_secondary_address(handle, addr_str);

            if (ret == MBUS_PROBE_COLLISION) {
                fprintf(stderr, "%s: Error: The address mask [%s] matches more than one device.\n", __PRETTY_FUNCTION__,
                        addr_str);
                continue;
            } else if (ret == MBUS_PROBE_NOTHING) {
                fprintf(stderr, "%s: Error: The selected secondary address does not match any device [%s].\n",
                        __PRETTY_FUNCTION__, addr_str);
                continue;
            } else if (ret == MBUS_PROBE_ERROR) {
                fprintf(stderr, "%s: Error: Failed to select secondary address [%s].\n", __PRETTY_FUNCTION__, addr_str);
                continue;
            }
            // else MBUS_PROBE_SINGLE

            address = MBUS_ADDRESS_NETWORK_LAYER;
        } else {
            // primary addressing
            address = atoi(addr_str);
        }

        if (mbus_send_request_frame(handle, address) == -1) {
            fprintf(stderr, "Failed to send M-Bus request frame.\n");
            continue;
        }

        if (mbus_recv_frame(handle, &reply) != MBUS_RECV_RESULT_OK) {
            fprintf(stderr, "Failed to receive M-Bus response frame.\n");
            continue;
        }

        //
        // dump hex data if debug is true
        //
        if (debug) {
            mbus_frame_print(&reply);
        }

        //
        // parse data
        //
        if (mbus_frame_data_parse(&reply, &reply_data) == -1) {
            fprintf(stderr, "M-bus data parse error: %s\n", mbus_error_str());
            continue;
        }

        struct tm time;
        double volume = 0;
        time_t timestamp = reply.timestamp;
        char *secondary_addr = mbus_frame_get_secondary_address(&reply);
        long secondary_addr_id = mbus_data_bcd_decode_hex(reply_data.data_var.header.id_bcd, 4);
        long long serial = 0;
        long long errors = 0;

        if (reply_data.type == MBUS_DATA_TYPE_VARIABLE) {
            int r;
            mbus_data_record *record;
            for (record = reply_data.data_var.record, r = 0; record; record = record->next, r++) {
                switch (record->drh.vib.vif) {
                    case 0x78: // Serial Number
                        serial = mbus_data_bcd_decode_hex(record->data, record->data_len);
                        break;
                    case 0x6D: // Date & Time
                        if (record->drh.dib.dif == 0x04) {
                            mbus_data_tm_decode(&time, record->data, record->data_len);
                        }
                        break;
                    case 0x10: // Volume, m³ * 0.000001
                    case 0x11: // Volume, m³ * 0.00001
                    case 0x12: // Volume, m³ * 0.0001
                    case 0x13: // Volume, m³ * 0.001
                    case 0x14: // Volume, m³ * 0.01
                    case 0x15: // Volume, m³ * 0.1
                    case 0x16: // Volume, m³ * 1
                    case 0x17: // Volume, m³ * 10
                        if ((record->drh.dib.dif & 0x40) >> 6 == 0) { // Storage Number is 0 (actual value)
                            volume = (
                                    strtod(mbus_data_record_value(record), NULL) *
                                    pow(10, record->drh.vib.vif - 0x16)
                            );
                        }
                        break;
                    case 0xFD:
                        // VIFE is 0x17 (Error Flags) & Storage Number is 0 (actual value)
                        if (record->drh.vib.nvife == 1 &&
                            record->drh.vib.vife[0] == 0x17 &&
                            (record->drh.dib.dif & 0x40) >> 6 == 0) {
                            errors = mbus_data_bcd_decode_hex(record->data, record->data_len);
                        }
                        break;
                }
            }
        }

        printf(
                "%s,%s,%lX,%llX,%.3f,%06llX,%ld\n",
                addr_str,
                secondary_addr,
                secondary_addr_id,
                serial,
                volume,
                errors,
                timestamp
        );

        // manual free
        if (reply_data.data_var.record) {
            mbus_data_record_free(reply_data.data_var.record); // free's up the whole list
        }

        i++;
        try_count = 0;
    }

    mbus_disconnect(handle);
    mbus_context_free(handle);
    return 0;
}
