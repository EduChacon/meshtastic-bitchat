#!/usr/bin/env python3
"""Apply a reviewed startup fix only to the pinned NimBLE-Arduino 1.4.3 sources."""

import hashlib
from pathlib import Path

PATCHES = {
    "NimBLEDevice.h": (
        "5835ceb64f1b0e204e6c65f8f9af5bfa47cacde331fe83d64beaf1e58f883137",
        [
            (
                'extern "C" void ble_store_config_init(void);',
                '#include <atomic>\n\nextern "C" void ble_store_config_init(void);',
            ),
            ("static bool        m_synced;", "static std::atomic<bool> m_synced;"),
        ],
    ),
    "NimBLEDevice.cpp": (
        "21aa9f02322869b0d6ff58f40bf8710e612d16f72bad55d5fa99d2da7feffd7f",
        [
            (
                "bool            NimBLEDevice::m_synced = false;",
                "std::atomic<bool> NimBLEDevice::m_synced{false};",
            ),
            (
                "        if (errRc == ESP_ERR_NVS_NO_FREE_PAGES || errRc == ESP_ERR_NVS_NEW_VERSION_FOUND) {\n"
                "            ESP_ERROR_CHECK(nvs_flash_erase());\n"
                "            errRc = nvs_flash_init();\n"
                "        }\n\n"
                "        ESP_ERROR_CHECK(errRc);",
                "        if (errRc != ESP_OK) {\n"
                '            NIMBLE_LOGE(LOG_TAG, "NVS unavailable; preserving stored data: %d", errRc);\n'
                "            return;\n"
                "        }",
            ),
            *[
                (
                    f"        ESP_ERROR_CHECK({call});",
                    f"        errRc = {call};\n"
                    "        if (errRc != ESP_OK) {\n"
                    f'            NIMBLE_LOGE(LOG_TAG, "{label} failed: %d", errRc);\n'
                    "            return;\n"
                    "        }",
                )
                for call, label in [
                    ("esp_bt_controller_init(&bt_cfg)", "controller init"),
                    ("esp_bt_controller_enable(ESP_BT_MODE_BLE)", "controller enable"),
                    ("esp_nimble_hci_init()", "HCI init"),
                ]
            ],
            (
                "        rc = ble_svc_gap_device_name_set(deviceName.c_str());\n        assert(rc == 0);",
                "        rc = ble_svc_gap_device_name_set(deviceName.c_str());\n"
                "        if (rc != 0) {\n"
                '            NIMBLE_LOGE(LOG_TAG, "GAP name setup failed: %d", rc);\n'
                "            return;\n"
                "        }",
            ),
            (
                "        nimble_port_freertos_init(NimBLEDevice::host_task);",
                "        if (esp_nimble_enable((void*)NimBLEDevice::host_task) != ESP_OK) {\n"
                '            NIMBLE_LOGE(LOG_TAG, "Host task creation failed");\n'
                "            return;\n"
                "        }",
            ),
            (
                "    while(!m_synced){\n        taskYIELD();\n    }",
                "    const TickType_t syncStart = xTaskGetTickCount();\n"
                "    while(!m_synced){\n"
                "        if (TickType_t(xTaskGetTickCount() - syncStart) >= pdMS_TO_TICKS(5000)) {\n"
                '            NIMBLE_LOGE(LOG_TAG, "Host synchronization timed out");\n'
                "            return;\n"
                "        }\n"
                "        vTaskDelay(1);\n"
                "    }",
            ),
        ],
    ),
    "nimble/porting/npl/freertos/src/nimble_port_freertos.c": (
        "868168f067c45c731cbf4dab4684b526c0e077d8de19d9388ebdc924d1c846e6",
        [
            (
                '    xTaskCreatePinnedToCore(host_task, "nimble_host", NIMBLE_HS_STACK_SIZE,\n'
                "                            NULL, (configMAX_PRIORITIES - 4), &host_task_h, NIMBLE_CORE);\n"
                "    return ESP_OK;",
                '    BaseType_t result = xTaskCreatePinnedToCore(host_task, "nimble_host", NIMBLE_HS_STACK_SIZE,\n'
                "                            NULL, (configMAX_PRIORITIES - 4), &host_task_h, NIMBLE_CORE);\n"
                "    return result == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;",
            )
        ],
    ),
}


def apply(library):
    pending = []
    for name, (expected, edits) in PATCHES.items():
        path = Path(library) / "src" / name
        current = path.read_text(encoding="utf-8")
        original = current
        for old, new in reversed(edits):
            if original.count(new) == 1:
                original = original.replace(new, old, 1)
        if hashlib.sha256(original.encode()).hexdigest() != expected:
            raise ValueError(f"Unrecognized NimBLE 1.4.3 source: {name}")
        updated = original
        for old, new in edits:
            if updated.count(old) != 1:
                raise ValueError(f"Ambiguous NimBLE patch: {name}")
            updated = updated.replace(old, new, 1)
        if current not in (original, updated):
            raise ValueError(f"Partial NimBLE patch: {name}")
        if current != updated:
            pending.append((path, updated))
    for path, updated in pending:
        path.write_text(updated, encoding="utf-8")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    apply(parser.parse_args().library)
