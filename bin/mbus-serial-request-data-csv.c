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

//
// init slave to get really the beginning of the records
//
static int
init_slaves(mbus_handle *handle)
{
    if (debug)
        printf("%s: debug: sending init frame #1\n", __PRETTY_FUNCTION__);

    if (mbus_send_ping_frame(handle, MBUS_ADDRESS_NETWORK_LAYER, 1) == -1)
    {
        return 0;
    }

    //
    // resend SND_NKE, maybe the first get lost
    //

    if (debug)
        printf("%s: debug: sending init frame #2\n", __PRETTY_FUNCTION__);

    if (mbus_send_ping_frame(handle, MBUS_ADDRESS_NETWORK_LAYER, 1) == -1)
    {
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
// Scan for devices using secondary addressing.
//------------------------------------------------------------------------------
int
main(int argc, char **argv)
{
    mbus_frame reply;
    mbus_frame_data reply_data;
    mbus_handle *handle = NULL;

    char *device, *addr_str;
    int address;
    long baudrate = 9600;
    int addr_c;
    int addr_base;

    memset((void *)&reply, 0, sizeof(mbus_frame));
    memset((void *)&reply_data, 0, sizeof(mbus_frame_data));

    if (argc >= 3)
    {
        int arg_pos = 1;
        if (strcmp(argv[arg_pos], "-d")) {
            debug = 1;
            arg_pos++;
        }
        if (strcmp(argv[arg_pos], "-b")) {
            arg_pos++;
            arg_pos++;
            baudrate = atol(argv[arg_pos]);
            arg_pos++;
        }
        device   = argv[arg_pos];
        arg_pos++;
        int base = arg_pos;
        addr_c = argc - base;
        addr_base = base;
    }
    else
    {
        fprintf(stderr, "usage: %s [-d] [-b BAUDRATE] device mbus-address\n", argv[0]);
        fprintf(stderr, "    optional flag -d for debug printout\n");
        fprintf(stderr, "    optional flag -b for selecting baudrate\n");
        return 0;
    }

    if ((handle = mbus_context_serial(device)) == NULL)
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
        fprintf(stderr,"Failed to setup connection to M-bus gateway\n");
        mbus_context_free(handle);
        return 1;
    }

    if (mbus_serial_set_baudrate(handle, baudrate) == -1)
    {
        fprintf(stderr,"Failed to set baud rate.\n");
        mbus_disconnect(handle);
        mbus_context_free(handle);
        return 1;
    }

    if (init_slaves(handle) == 0)
    {
        mbus_disconnect(handle);
        mbus_context_free(handle);
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
                mbus_disconnect(handle);
                mbus_context_free(handle);
                return 1;
            } else if (ret == MBUS_PROBE_NOTHING) {
                fprintf(stderr, "%s: Error: The selected secondary address does not match any device [%s].\n",
                        __PRETTY_FUNCTION__, addr_str);
                mbus_disconnect(handle);
                mbus_context_free(handle);
                return 1;
            } else if (ret == MBUS_PROBE_ERROR) {
                fprintf(stderr, "%s: Error: Failed to select secondary address [%s].\n", __PRETTY_FUNCTION__, addr_str);
                mbus_disconnect(handle);
                mbus_context_free(handle);
                return 1;
            }
            // else MBUS_PROBE_SINGLE

            address = MBUS_ADDRESS_NETWORK_LAYER;
        } else {
            // primary addressing
            address = atoi(addr_str);
        }

        if (mbus_send_request_frame(handle, address) == -1) {
            fprintf(stderr, "Failed to send M-Bus request frame.\n");
            mbus_disconnect(handle);
            mbus_context_free(handle);
            return 1;
        }

        if (mbus_recv_frame(handle, &reply) != MBUS_RECV_RESULT_OK) {
            fprintf(stderr, "Failed to receive M-Bus response frame.\n");
            return 1;
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
            mbus_disconnect(handle);
            mbus_context_free(handle);
            return 1;
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
