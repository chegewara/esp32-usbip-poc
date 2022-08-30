#include <stdio.h>
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"


#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <esp_vfs_fat.h>
#include "nvs_flash.h"
#include "usbip.hpp"


extern "C" void start_server();
USBhost* host;
static USBipDevice* device;
static bool is_ready = false;
static USBIP usbip;

void client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    ESP_LOGW("", "usb_host_client_event_msg_t event: %d", event_msg->event);
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
    {
        host->open(event_msg);

        info = host->getDeviceInfo();
        ESP_LOGI("USB_HOST_CLIENT_EVENT_NEW_DEV", "device speed: %s, device address: %d, max ep_ctrl size: %d, config: %d", info.speed ? "USB_SPEED_FULL" : "USB_SPEED_LOW", info.dev_addr, info.bMaxPacketSize0, info.bConfigurationValue);
        dev_desc = host->getDeviceDescriptor();
        
        device = new USBipDevice();
        device->init(host);

        is_ready = true;
    }
    else
    {
        // TODO: release all interfaces claimed in device.init
        is_ready = false;
        device->deinit();
        delete(device);
    }
}

void init_usbip()
{
    host = new USBhost();
    host->registerClientCb(client_event_callback);
    host->init();
}

extern "C" void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("*", ESP_LOG_NONE);
    // esp_log_level_set("USB_EPx_RESP", ESP_LOG_NONE);
    // esp_log_level_set("example", ESP_LOG_INFO);
    init_usbip();

    start_server();
}
