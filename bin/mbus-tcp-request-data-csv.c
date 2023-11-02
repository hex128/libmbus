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
    int addr_c;
    int addr_base;

    memset((void *) &reply, 0, sizeof(mbus_frame));
    memset((void *) &reply_data, 0, sizeof(mbus_frame_data));

    if (argc >= 4) {
        int base = 0;
        debug = 0;
        if (strcmp(argv[1], "-d") == 0) {
            debug = 1;
            base++;
        }
        host = argv[base + 1];
        port = atol(argv[base + 2]);
        addr_c = argc - base - 3;
        addr_base = 3 + base;
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

    for (int i = 0; i < addr_c; i++) {
        if (addr_base == -1) {
            sprintf(seq_addr_str, "%d", i + 1);
            addr_str = seq_addr_str;
        } else {
            addr_str = argv[addr_base + i];
        }
        // addr_str = argv[addr_base + i];
        fprintf(stderr, "Reading address %s\n", addr_str);

        if (mbus_is_secondary_address(addr_str)) {
            // secondary addressing

            int ret;

            ret = mbus_select_secondary_address(handle, addr_str);

            if (ret == MBUS_PROBE_COLLISION) {
                fprintf(stderr, "%s: Error: The address mask [%s] matches more than one device.\n", __PRETTY_FUNCTION__,
                        addr_str);
                printf("%s,-,-\n", addr_str);
                continue;
            } else if (ret == MBUS_PROBE_NOTHING) {
                fprintf(stderr, "%s: Error: The selected secondary address does not match any device [%s].\n",
                        __PRETTY_FUNCTION__, addr_str);
                printf("%s,-,-\n", addr_str);
                continue;
            } else if (ret == MBUS_PROBE_ERROR) {
                fprintf(stderr, "%s: Error: Failed to select secondary address [%s].\n", __PRETTY_FUNCTION__, addr_str);
                printf("%s,-,-\n", addr_str);
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
            printf("%s,-,-\n", addr_str);
            continue;
        }

        if (mbus_recv_frame(handle, &reply) != MBUS_RECV_RESULT_OK) {
            fprintf(stderr, "Failed to receive M-Bus response frame.\n");
            printf("%s,-,-\n", addr_str);
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
            printf("%s,-,-\n", addr_str);
            continue;
        }

        struct tm time;
        double volume = 0;
        time_t timestamp = 0;
        long long serial = 0;
        long long errors = 0;

        if (reply_data.type == MBUS_DATA_TYPE_VARIABLE) {
            int r;
            mbus_data_record *record;
            for (record = reply_data.data_var.record, r = 0; record; record = record->next, r++) {
                switch (record->drh.vib.vif) {
                    case 0x6D:
                        if (record->drh.dib.dif == 0x04) {
                            mbus_data_tm_decode(&time, record->data, record->data_len);
                        }
                        timestamp = record->timestamp;
                        break;
                    case 0x78:
                        serial = mbus_data_bcd_decode_hex(record->data, record->data_len);
                        break;
                    case 0x13:
                        if (record->drh.dib.dif == 0x04) {
                            volume = strtod(mbus_data_record_value(record), NULL) / 1000;
                        }
                        break;
                    case 0xFD:
                        if (record->drh.vib.nvife == 1 && record->drh.vib.vife[0] == 0x17) {
                            errors = mbus_data_bcd_decode_hex(record->data, record->data_len);
                        }
                        break;
                }
            }
        }

        printf(
                "%s,%llX,%.3f,%06llX,%ld\n",
                addr_str,
                serial,
                volume,
                errors,
                timestamp
        );

        // manual free
        if (reply_data.data_var.record) {
            mbus_data_record_free(reply_data.data_var.record); // free's up the whole list
        }
    }

    mbus_disconnect(handle);
    mbus_context_free(handle);
    return 0;
}
