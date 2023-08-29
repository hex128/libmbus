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
main(int argc, char **argv)
{
    mbus_frame reply;
    mbus_frame_data reply_data;
    mbus_handle *handle = NULL;

    char *host, *addr_str;
    int address;
    long port;
    int addr_c;
    int addr_base;

    memset((void *)&reply, 0, sizeof(mbus_frame));
    memset((void *)&reply_data, 0, sizeof(mbus_frame_data));

    if (argc >= 4)
    {
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
    }
    else
    {
        fprintf(stderr, "usage: %s [-d] host port mbus-address\n", argv[0]);
        fprintf(stderr, "    optional flag -d for debug printout\n");
        return 0;
    }

    if ((port < 0) || (port > 0xFFFF))
    {
        fprintf(stderr, "Invalid port: %ld\n", port);
        return 1;
    }

    if ((handle = mbus_context_tcp(host, port)) == NULL)
    {
        fprintf(stderr, "Could not initialize M-Bus context: %s\n",  mbus_error_str());
        return 1;
    }
    
    if (debug)
    {
        mbus_register_send_event(handle, &mbus_dump_send_event);
        mbus_register_recv_event(handle, &mbus_dump_recv_event);
    }

    if (mbus_connect(handle) == -1)
    {
        fprintf(stderr, "Failed to setup connection to M-bus gateway\n");
        return 1;
    }

    for (int i = 0; i < addr_c; i++) {
        addr_str = argv[addr_base + i];
        if (mbus_is_secondary_address(addr_str)) {
            // secondary addressing

            int ret;

            ret = mbus_select_secondary_address(handle, addr_str);

            if (ret == MBUS_PROBE_COLLISION) {
                fprintf(stderr, "%s: Error: The address mask [%s] matches more than one device.\n", __PRETTY_FUNCTION__,
                        addr_str);
                continue;
                //return 1;
            } else if (ret == MBUS_PROBE_NOTHING) {
                fprintf(stderr, "%s: Error: The selected secondary address does not match any device [%s].\n",
                        __PRETTY_FUNCTION__, addr_str);
                continue;
                //return 1;
            } else if (ret == MBUS_PROBE_ERROR) {
                fprintf(stderr, "%s: Error: Failed to select secondary address [%s].\n", __PRETTY_FUNCTION__, addr_str);
                continue;
                //return 1;
            }
            // else MBUS_PROBE_SINGLE

            if (mbus_send_request_frame(handle, MBUS_ADDRESS_NETWORK_LAYER) == -1) {
                fprintf(stderr, "Failed to send M-Bus request frame.\n");
                continue;
                //return 1;
            }
        } else {
            // primary addressing

            address = atoi(addr_str);
            if (mbus_send_request_frame(handle, address) == -1) {
                fprintf(stderr, "Failed to send M-Bus request frame.\n");
                continue;
                //return 1;
            }
        }

        if (mbus_recv_frame(handle, &reply) != MBUS_RECV_RESULT_OK) {
            fprintf(stderr, "Failed to receive M-Bus response frame: %s\n", mbus_error_str());
            continue;
            //return 1;
        }

        //
        // parse data and print in XML format
        //
        if (debug) {
            mbus_frame_print(&reply);
        }

        if (mbus_frame_data_parse(&reply, &reply_data) == -1) {
            fprintf(stderr, "M-bus data parse error: %s\n", mbus_error_str());
            continue;
            //return 1;
        }

        if (reply_data.type == MBUS_DATA_TYPE_VARIABLE)
        {
            mbus_data_record *record;
            for (record = reply_data.data_var.record, i = 0; record; record = record->next, i++) {
                if (0x10 <= record->drh.vib.vif && record->drh.vib.vif <= 0x17) {
                    printf("%s,%s\n", addr_str, mbus_data_record_value(record));
                }
            }
        }

        // manual free
        if (reply_data.data_var.record) {
            mbus_data_record_free(reply_data.data_var.record); // free's up the whole list
        }
    }

    mbus_disconnect(handle);
    mbus_context_free(handle);
    return 0;
}
