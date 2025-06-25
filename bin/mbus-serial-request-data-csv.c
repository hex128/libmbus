#include <string.h>

#include <stdio.h>
#include <math.h>
#include <mbus/mbus.h>

static int debug = 0;

static int
usage(char *name, const int rc) {
    fprintf(stderr, "usage: %s [-d] [-b BAUDRATE] [-r RETRIES] DEVICE [ADDRESS...]\n", name);
    fprintf(stderr, "    optional flag -d for debug printout\n");
    fprintf(stderr, "    optional flag -b for selecting baudrate\n");
    fprintf(stderr, "    optional flag -r for retry count\n");
    return rc;
}

//
// pad a secondary address if applicable
//
static void
normalize_address(const char *addr_str, char *output_buffer) {
    const int addr_num = atoi(addr_str);
    if (addr_num >= 0 && addr_num <= 250) {
        snprintf(output_buffer, 17, "%d", addr_num);
    } else {
        strncpy(output_buffer, addr_str, 16);
        output_buffer[16] = '\0';

        // Pad with leading zeros until the length is 8
        while (strlen(output_buffer) < 8) {
            memmove(output_buffer + 1, output_buffer, strlen(output_buffer) + 1);
            output_buffer[0] = '0';
        }

        // Pad with 'F' until the length is 16
        size_t len = strlen(output_buffer);
        while (len < 16) {
            output_buffer[len] = 'F';
            len++;
        }
        output_buffer[len] = '\0';
    }
}

//
// init slave to get really the beginning of the records
//
static int
init_slaves(mbus_handle *handle) {
    if (debug)
        fprintf(stderr, "%s: debug: sending init frame #1\n", __PRETTY_FUNCTION__);

    if (mbus_send_ping_frame(handle, MBUS_ADDRESS_NETWORK_LAYER, 1) == -1) {
        return 0;
    }

    //
    // resend SND_NKE, maybe the first get lost
    //

    if (debug)
        fprintf(stderr, "%s: debug: sending init frame #2\n", __PRETTY_FUNCTION__);

    if (mbus_send_ping_frame(handle, MBUS_ADDRESS_NETWORK_LAYER, 1) == -1) {
        return 0;
    }

    return 1;
}

int
main(int argc, char **argv) {
    mbus_frame reply;
    mbus_frame_data reply_data;
    mbus_handle *handle = NULL;

    char *device, *addr_str = "0";
    char normalized_address[17];
    int address;
    long baudrate = 2400;
    int max_tries = 1;
    int addr_c;
    int addr_base;

    char seq_addr_str[4] = "0";
    int i = 0;
    int try_count = 0;

    memset(&reply, 0, sizeof(mbus_frame));
    memset(&reply_data, 0, sizeof(mbus_frame_data));

    if (argc >= 2) {
        int arg_pos = 1;
        if (arg_pos < argc && strcmp(argv[arg_pos], "-h") == 0) {
            return usage(argv[0], 0);
        }
        if (arg_pos < argc && strcmp(argv[arg_pos], "-d") == 0) {
            debug = 1;
            arg_pos++;
        }
        if (arg_pos < argc && strcmp(argv[arg_pos], "-b") == 0) {
            arg_pos++;
            if (arg_pos >= argc) {
                return usage(argv[0], 1);
            }
            baudrate = atol(argv[arg_pos]);
            arg_pos++;
        }
        if (arg_pos < argc && strcmp(argv[arg_pos], "-r") == 0) {
            arg_pos++;
            if (arg_pos >= argc) {
                return usage(argv[0], 1);
            }
            max_tries = atoi(argv[arg_pos]);
            arg_pos++;
        }
        if (arg_pos >= argc) {
            return usage(argv[0], 1);
        }
        device = argv[arg_pos];
        arg_pos++;
        if (arg_pos == argc) {
            addr_base = -1;
            addr_c = 250;
        } else {
            addr_c = argc - arg_pos;
            addr_base = arg_pos;
        }
    } else {
        return usage(argv[0], 1);
    }

    if ((handle = mbus_context_serial(device)) == NULL) {
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

    if (mbus_serial_set_baudrate(handle, baudrate) == -1) {
        fprintf(stderr, "Failed to set baud rate.\n");
        mbus_disconnect(handle);
        mbus_context_free(handle);
        return 1;
    }

    if (init_slaves(handle) == 0) {
        fprintf(stderr, "Failed to initialize slaves\n");
        mbus_disconnect(handle);
        mbus_context_free(handle);
        return 1;
    }

    while (i < addr_c) {
        if (try_count > 0) {
            init_slaves(handle);
            mbus_recv_frame(handle, &reply);
        }
        try_count++;
        if (try_count > max_tries) {
            printf("%s,,,,,,\n", addr_str);
            fflush(stdout);
            i++;
            try_count = 0;
            continue;
        }
        if (addr_base == -1) {
            snprintf(seq_addr_str, sizeof(seq_addr_str), "%d", i + 1);
            addr_str = seq_addr_str;
        } else {
            addr_str = argv[addr_base + i];
        }
        fprintf(stderr,
                "Reading address %s (%d of %d); attempt %d of %d\n",
                addr_str, i + 1, addr_c, try_count, max_tries);

        normalize_address(addr_str, normalized_address);
        if (mbus_is_secondary_address(normalized_address)) {
            // secondary addressing

            int ret;

            ret = mbus_select_secondary_address(handle, normalized_address);

            if (ret == MBUS_PROBE_COLLISION) {
                fprintf(stderr, "%s: Error: The address mask [%s] matches more than one device.\n",
                        __PRETTY_FUNCTION__, normalized_address);
                continue;
            }
            if (ret == MBUS_PROBE_NOTHING) {
                fprintf(stderr, "%s: Error: The selected secondary address does not match any device [%s].\n",
                        __PRETTY_FUNCTION__, normalized_address);
                continue;
            }
            if (ret == MBUS_PROBE_ERROR) {
                fprintf(stderr, "%s: Error: Failed to select secondary address [%s].\n",
                        __PRETTY_FUNCTION__, normalized_address);
                continue;
            }
            // else MBUS_PROBE_SINGLE

            address = MBUS_ADDRESS_NETWORK_LAYER;
        } else {
            // primary addressing
            address = atoi(normalized_address);
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
                        // Storage Number is 0 (actual value)
                        if ((record->drh.dib.dif & 0x40) >> 6 == 0 && record->drh.dib.ndife == 0) {
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
                    default:
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
        fflush(stdout);

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
