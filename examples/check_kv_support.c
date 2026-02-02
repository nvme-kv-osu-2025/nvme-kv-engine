/**
 * NVMe KV Support Checker
 *
 * Standalone program that checks whether the target NVMe SSD
 * supports KV operations by:
 *   1. Opening the device via the KVSSD API
 *   2. Querying device info and capabilities
 *   3. Attempting a store / retrieve / delete round-trip
 */

#include <kvs_api.h>
#include <kvs_struct.h>
#include <kvs_const.h>
#include <kvs_result.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper: map kvs_result to human-readable string */
static const char *kvs_strerror(kvs_result r) {
    switch (r) {
        case KVS_SUCCESS:                return "SUCCESS";
        case KVS_ERR_DEV_NOT_EXIST:      return "DEVICE_NOT_EXIST";
        case KVS_ERR_KS_NOT_EXIST:       return "KEYSPACE_NOT_EXIST";
        case KVS_ERR_KEY_NOT_EXIST:      return "KEY_NOT_EXIST";
        case KVS_ERR_KEY_LENGTH_INVALID: return "KEY_LENGTH_INVALID";
        case KVS_ERR_VALUE_LENGTH_INVALID: return "VALUE_LENGTH_INVALID";
        default:                         return "UNKNOWN_ERROR";
    }
}

static void print_separator(void) {
    printf("--------------------------------------------\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device_path>\n", argv[0]);
        fprintf(stderr, "  Real device:  %s /dev/nvmeXnY\n", argv[0]);
        fprintf(stderr, "  Emulator:     %s /dev/kvemul\n", argv[0]);
        return 1;
    }

    const char *dev_path = argv[1];
    kvs_result ret;

    /* ---- Step 1: Open the device ---- */
    printf("=== NVMe KV Support Check ===\n");
    print_separator();
    printf("[1] Opening device: %s\n", dev_path);

    kvs_device_handle dev_hd;
    ret = kvs_open_device((char *)dev_path, &dev_hd);
    if (ret != KVS_SUCCESS) {
        fprintf(stderr, "    FAIL - kvs_open_device returned %d (%s)\n",
                ret, kvs_strerror(ret));
        fprintf(stderr, "\n** Device does NOT support KV operations "
                        "(or path is invalid). **\n");
        return 1;
    }
    printf("    OK   - device opened successfully\n");

    /* ---- Step 2: Query device info ---- */
    print_separator();
    printf("[2] Querying device capabilities\n");

    kvs_device dev_info;
    memset(&dev_info, 0, sizeof(dev_info));
    ret = kvs_get_device_info(dev_hd, &dev_info);
    if (ret != KVS_SUCCESS) {
        fprintf(stderr, "    WARN - kvs_get_device_info returned %d (%s)\n",
                ret, kvs_strerror(ret));
    } else {
        printf("    Capacity           : %lu bytes (%.2f GB)\n",
               (unsigned long)dev_info.capacity,
               (double)dev_info.capacity / (1024.0 * 1024.0 * 1024.0));
        printf("    Unallocated        : %lu bytes\n",
               (unsigned long)dev_info.unalloc_capacity);
        printf("    Max key length     : %u bytes\n", dev_info.max_key_len);
        printf("    Max value length   : %u bytes (%u KB)\n",
               dev_info.max_value_len,
               dev_info.max_value_len / 1024);
        printf("    Optimal value len  : %u bytes\n",
               dev_info.optimal_value_len);
    }

    /* ---- Step 3: Open / create a key space ---- */
    print_separator();
    printf("[3] Opening default key space\n");

    kvs_key_space_handle ks_hd;
    ret = kvs_open_key_space(dev_hd, "test_ks", &ks_hd);
    if (ret == KVS_ERR_KS_NOT_EXIST) {
        printf("    Key space 'test_ks' doesn't exist — creating it\n");
        kvs_key_space_name ks_name;
        ks_name.name_len = strlen("test_ks");
        ks_name.name = "test_ks";
        kvs_option_key_space ks_opt = { .option = KVS_KEY_ORDER_NONE };
        ret = kvs_create_key_space(dev_hd, &ks_name, 0, ks_opt);
        if (ret != KVS_SUCCESS) {
            fprintf(stderr, "    FAIL - kvs_create_key_space returned %d (%s)\n",
                    ret, kvs_strerror(ret));
            kvs_close_device(dev_hd);
            return 1;
        }
        ret = kvs_open_key_space(dev_hd, "test_ks", &ks_hd);
    }
    if (ret != KVS_SUCCESS) {
        fprintf(stderr, "    FAIL - kvs_open_key_space returned %d (%s)\n",
                ret, kvs_strerror(ret));
        kvs_close_device(dev_hd);
        return 1;
    }
    printf("    OK   - key space opened\n");

    /* ---- Step 4: Store a test KV pair ---- */
    print_separator();
    printf("[4] Storing test KV pair\n");

    const char *test_key_str = "kv_check_key";
    const char *test_val_str = "kv_support_check_value_12345";

    kvs_key kv_key;
    kv_key.key = (void *)test_key_str;
    kv_key.length = (uint16_t)strlen(test_key_str);

    kvs_value kv_val;
    kv_val.value = (void *)test_val_str;
    kv_val.length = (uint32_t)strlen(test_val_str);
    kv_val.actual_value_size = 0;
    kv_val.offset = 0;

    kvs_option_store store_opt = { .st_type = KVS_STORE_POST, .assoc = NULL };
    ret = kvs_store_kvp(ks_hd, &kv_key, &kv_val, &store_opt);
    if (ret != KVS_SUCCESS) {
        fprintf(stderr, "    FAIL - kvs_store_kvp returned %d (%s)\n",
                ret, kvs_strerror(ret));
        fprintf(stderr, "\n** Device opened but KV STORE failed. **\n");
        kvs_close_key_space(ks_hd);
        kvs_close_device(dev_hd);
        return 1;
    }
    printf("    OK   - stored key '%s' (%zu bytes)\n",
           test_key_str, strlen(test_val_str));

    /* ---- Step 5: Retrieve the KV pair ---- */
    print_separator();
    printf("[5] Retrieving test KV pair\n");

    char retrieve_buf[256];
    memset(retrieve_buf, 0, sizeof(retrieve_buf));

    kvs_value kv_ret_val;
    kv_ret_val.value = retrieve_buf;
    kv_ret_val.length = sizeof(retrieve_buf);
    kv_ret_val.actual_value_size = 0;
    kv_ret_val.offset = 0;

    kvs_option_retrieve ret_opt = { .kvs_retrieve_delete = false };
    ret = kvs_retrieve_kvp(ks_hd, &kv_key, &ret_opt, &kv_ret_val);
    if (ret != KVS_SUCCESS) {
        fprintf(stderr, "    FAIL - kvs_retrieve_kvp returned %d (%s)\n",
                ret, kvs_strerror(ret));
        kvs_close_key_space(ks_hd);
        kvs_close_device(dev_hd);
        return 1;
    }

    int data_match = (memcmp(retrieve_buf, test_val_str, strlen(test_val_str)) == 0);
    printf("    OK   - retrieved %u bytes, data match: %s\n",
           kv_ret_val.actual_value_size,
           data_match ? "YES" : "NO");

    /* ---- Step 6: Delete the KV pair ---- */
    print_separator();
    printf("[6] Deleting test KV pair\n");

    kvs_option_delete del_opt = { .kvs_delete_error = true };
    ret = kvs_delete_kvp(ks_hd, &kv_key, &del_opt);
    if (ret != KVS_SUCCESS) {
        fprintf(stderr, "    WARN - kvs_delete_kvp returned %d (%s)\n",
                ret, kvs_strerror(ret));
    } else {
        printf("    OK   - key deleted\n");
    }

    /* ---- Cleanup ---- */
    kvs_close_key_space(ks_hd);
    kvs_close_device(dev_hd);

    /* ---- Verdict ---- */
    print_separator();
    if (data_match) {
        printf("\n** PASS — Device supports KV operations! **\n\n");
        return 0;
    } else {
        printf("\n** FAIL — Store/Retrieve round-trip data mismatch. **\n\n");
        return 1;
    }
}
