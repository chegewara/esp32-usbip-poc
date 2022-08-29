#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include "esp_log.h"
#include "esp_event.h"

#include "usbip.hpp"

#define TAG "usbip"
usb_device_info_t info;
const usb_device_desc_t *dev_desc;
static esp_event_loop_handle_t loop_handle;
static SemaphoreHandle_t usb_sem;
static SemaphoreHandle_t usb_sem1;
static int _sock;

static bool is_ready = false;
static bool finished = false;
static usb_transfer_t *_transfer;

ESP_EVENT_DEFINE_BASE(USBIP_EVENT_BASE);
#define USB_CTRL_RESP   0x1001
#define USB_EPx_RESP    0x1002

static void usb_ctrl_cb(usb_transfer_t *transfer)
{
    // ESP_LOG_BUFFER_HEX("TAG 0", transfer->data_buffer, transfer->actual_num_bytes);
    // finished = true;
    size_t len1 = transfer->actual_num_bytes;
    // esp_rom_printf("actual num bytes 0: %d\n", len1);
    esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USB_CTRL_RESP, (void*)&transfer, sizeof(usb_transfer_t*), 0);
    // xSemaphoreGive(usb_sem1);
}

static void usb_read_cb(usb_transfer_t *transfer)
{
    ESP_LOG_BUFFER_HEX("usb_read_cb", transfer->data_buffer, transfer->actual_num_bytes);

    size_t len1 = transfer->actual_num_bytes;
    // esp_rom_printf("actual num bytes 1: %d\n", len1);
    esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USB_EPx_RESP, (void*)&transfer, sizeof(usb_transfer_t*), 0);
    // xSemaphoreGive(usb_sem1);
}

static void _event_handler(void* event_handler_arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    // printf("%s: 0x%04x\n\n\n\n\n\n\n\n", event_base, event_id);
    switch (event_id)
    {
    case USB_CTRL_RESP:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        usb_transfer_t *transfer = *(usb_transfer_t **)event_data;
        usbip_submit_t* req = (usbip_submit_t*)transfer->context;
        int _len = transfer->actual_num_bytes - 8; //__bswap_32(req->length);
        if(_len < 0) {
            dev->deallocate(transfer);   
            break;
        }
        if (req->header.direction == 0) // 0: USBIP_DIR_OUT
        {
            _len = 0;
        }

        req->header.command = __bswap_32(USBIP_RET_SUBMIT);
        // for server (response), this shall be set to 0
        req->header.devid = 0;
        // only used by client, for server this shall be 0
        req->header.direction = 0;
        // ep: endpoint number only used by client, for server this shall be 0
        req->header.ep = 0;
        req->status = 0;
        // TODO: transfer_buffer. If direction is USBIP_DIR_IN then n equals actual_length; 
        // otherwise n equals 0. 
        // For ISO transfers the padding between each ISO packets is not transmitted.
        req->length = __bswap_32(_len);
        req->padding = 0;
        if(_len) memcpy(&req->transfer_buffer[0], transfer->data_buffer + 8, _len);
        // req->transfer_buffer = transfer->data_buffer + 8;
        if(transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            _len = 0;
            req->length = 0;
            req->status = -ETIME;
            req->error_count = 1;
        }
        int to_write = 0x30 + _len;
        ESP_LOG_BUFFER_HEX_LEVEL("USB_CTRL_RESP", (void*)req, to_write, ESP_LOG_WARN);
        int written = write(_sock, (void*)req, to_write);
        delete req;
        dev->deallocate(transfer);
        break;
    }

    case USB_EPx_RESP:{
        ESP_LOGE("", "here");
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        usb_transfer_t *transfer = *(usb_transfer_t **)event_data;
        usbip_submit_t* req = (usbip_submit_t*)transfer->context;
        int _len = transfer->actual_num_bytes;
        if(_len <= 0) {
            dev->deallocate(transfer);   
            break;
        }
        if (req->header.direction == 0)
        {
            _len = 0;
        }

        req->header.command = __bswap_32(USBIP_RET_SUBMIT);
        // for server (response), this shall be set to 0
        req->header.devid = 0;
        // only used by client, for server this shall be 0
        req->header.direction = 0;
        // ep: endpoint number only used by client, for server this shall be 0
        req->header.ep = 0;
        req->status = 0;
        // TODO: transfer_buffer. If direction is USBIP_DIR_IN then n equals actual_length; 
        // otherwise n equals 0. 
        // For ISO transfers the padding between each ISO packets is not transmitted.
        req->length = __bswap_32(_len);
        req->start_frame = 0;
        req->padding = 0;
        memcpy(&req->transfer_buffer[0], transfer->data_buffer, transfer->actual_num_bytes);
        if(transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
            _len = 0;
            req->length = 0;
            req->status = -ETIME;
            req->error_count = 1;
        }
        int to_write = 0x30 + _len;
        ESP_LOG_BUFFER_HEX_LEVEL("USB_EPx_RESP", (void*)req, to_write, ESP_LOG_WARN);

        int written = write(_sock, (void*)req, to_write);
        delete req;
        dev->deallocate(transfer);
        break;
    }

    case OP_REQ_DEVLIST:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        usbip_devlist_t resp = {};
        resp.request.version = USBIP_VERSION;
        resp.request.command = __bswap_16(OP_REP_DEVLIST);
        resp.request.status = 0;
        strcpy(resp.path, "/espressif/usbip/usb1");
        strcpy(resp.busid, "1-1");
        resp.busnum = 1;
        resp.devnum = 1;
        resp.count = 1;
        dev->fill_list_data(&resp);

        int to_write = 12;
        if(resp.count > 0) to_write = resp.count * (0x0c + 0x138 + resp.bNumInterfaces * 4);
        int written = write(_sock, (void*)&resp, to_write);
        break;
    }

    case OP_REQ_IMPORT:{
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        usbip_import_t resp = {};
        resp.request.version = USBIP_VERSION;
        resp.request.command = __bswap_16(OP_REP_IMPORT);
        resp.request.status = 0;
        strcpy(resp.path, "/espressif/usbip/usb1");
        strcpy(resp.busid, "1-1");
        resp.busnum = 1;
        resp.devnum = 1;
        dev->fill_import_data(&resp);

        int to_write = sizeof(usbip_import_t);
        int written = write(_sock, (void*)&resp, to_write);
        break;
    }

    case USBIP_CMD_SUBMIT:{
        xSemaphoreTake(usb_sem, 10000);
        USBipDevice* dev = (USBipDevice*)event_handler_arg;
        urb_data_t* data = (urb_data_t*)event_data;
        uint8_t* rx_buffer = (uint8_t*) data->rx_buffer;
        int len = data->len;
        int start = 0;
        int _len = len;
        ESP_LOG_BUFFER_HEX_LEVEL("SUBMIT 1", rx_buffer, 48, ESP_LOG_WARN);

        do
        {
            ESP_LOGW(TAG, "USBIP_CMD_SUBMIT: start 0x%02x, len: %d", start, _len);
            ESP_LOG_BUFFER_HEX("SUBMIT", rx_buffer + start, 48);

            usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer + start);
            usbip_submit_t* req = new usbip_submit_t(); // make it heap caps malloc
            int tl = 0;
            if(_req->header.direction == 0)
                tl = __bswap_32(_req->length);
            memcpy(req, _req, 0x30 + tl);
            
            ESP_LOGW(TAG, "request ep: %d", __bswap_32(req->header.ep));
            int tlen = 0;

            if(req->header.ep == 0){ // EP0
                tlen = dev->req_ctrl_xfer(req);
                if(tlen > 0){
                    ESP_LOG_BUFFER_HEX_LEVEL("SUBMIT 7", rx_buffer + start, 48 + tlen, ESP_LOG_ERROR);
                // printf("\ttlen 1: %d\n\n", tlen);
                }
                tlen = 0;
            } else { // EPx
                tlen = dev->req_ep_xfer(req);
                if(tlen > 0){
                    ESP_LOG_BUFFER_HEX_LEVEL("SUBMIT 10", rx_buffer + start, 48 + tlen, ESP_LOG_ERROR);
                // printf("\ttlen 2: %d/%d/%d\n\n", tlen, _len, len);
                }
            }
            start += 0x30 + tlen;
            _len -= 0x30 + tlen;
            ESP_LOGI(TAG, "USBIP_CMD_SUBMIT: end 0x%02x, len: %d", start, _len);
        } 
        // while(0);
        while (_len >= 0x30);
        xSemaphoreGive(usb_sem);
        // printf("done\n");
        break;
    }

    case USBIP_CMD_UNLINK:{
        usbip_unlink_t* req = *(usbip_unlink_t**)event_data;
        req->header.command = __bswap_32(USBIP_RET_UNLINK);
        // for server (response), this shall be set to 0
        req->header.devid = 0;
        // only used by client, for server this shall be 0
        req->header.direction = 0;
        // ep: endpoint number only used by client, for UNLINK, this shall be 0
        req->header.ep = 0;
        req->status = 0;
        int to_write = 48;
        int written = write(_sock, (void*)req, to_write);
        ESP_LOG_BUFFER_HEX(TAG, (void*)req, 48);
        delete req;
        break;
    }

    default:
        break;
    }
}


USBipDevice::USBipDevice()
{
    // device = this;
    usb_sem = xSemaphoreCreateBinary();
    usb_sem1 = xSemaphoreCreateBinary();
    xSemaphoreGive(usb_sem);
    xSemaphoreGive(usb_sem1);

    esp_event_loop_args_t loop_args = {
        .queue_size = 100,
        .task_name = "usbip_events_task",
        .task_priority = 21,
        .task_stack_size = 4*1024,
        .task_core_id = 1
    };

    esp_event_loop_create(&loop_args, &loop_handle);

    esp_event_handler_register_with(loop_handle, USBIP_EVENT_BASE, ESP_EVENT_ANY_ID, _event_handler, this);
}

USBipDevice::~USBipDevice()
{
}

bool USBipDevice::init(USBhost* host)
{
    _host = host;

    usb_device_info_t info = host->getDeviceInfo();
    USBhostDevice::init(1032);
    xfer_ctrl->callback = usb_ctrl_cb;

    config_desc = host->getConfigurationDescriptor();
    
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        const usb_ep_desc_t *ep = nullptr;

        for (size_t i = 0; i < intf->bNumEndpoints; i++)
        {
            int _offset = 0;
            ep = usb_parse_endpoint_descriptor_by_index(intf, i, config_desc->wTotalLength, &_offset);
            uint8_t adr = ep->bEndpointAddress;
            if (adr & 0x80)
            {
                endpoints[adr & 0xf][1] = ep;
            } else {
                endpoints[adr & 0xf][0] = ep;
            }

            printf("EP num: %d/%d, len: %d, ", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
            if (ep)
                printf("address: 0x%02x, EP max size: %d, dir: %s\n", ep->bEndpointAddress, ep->wMaxPacketSize, (ep->bEndpointAddress & 0x80) ? "IN" : "OUT");
            else
                ESP_LOGW("", "error to parse endpoint by index; EP num: %d/%d, len: %d", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
        }
        esp_err_t err = usb_host_interface_claim(_host->clientHandle(), _host->deviceHandle(), n, 0);
        ESP_LOGI("", "interface claim status: %d", err);
    }

    return true;
}

void USBipDevice::fill_import_data(usbip_import_t* resp)
{
    resp->speed = info.speed ? 0x02000000 : 0x01000000;
    resp->idVendor = dev_desc->idVendor;
    resp->idProduct = dev_desc->idProduct;
    resp->bcdDevice = dev_desc->bcdDevice;
    resp->bDeviceClass = dev_desc->bDeviceClass;
    resp->bDeviceSubClass = dev_desc->bDeviceSubClass;
    resp->bDeviceProtocol = dev_desc->bDeviceProtocol;
    resp->bConfigurationValue = config_desc->bConfigurationValue;
    resp->bNumConfigurations = dev_desc->bNumConfigurations;
    resp->bNumInterfaces = config_desc->bNumInterfaces;
}

void USBipDevice::fill_list_data(usbip_devlist_t* resp)
{
    int offset = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset);
        resp->intfs[n].bInterfaceClass = intf->bInterfaceClass,
        resp->intfs[n].bInterfaceSubClass = intf->bInterfaceSubClass,
        resp->intfs[n].bInterfaceProtocol = intf->bInterfaceProtocol,
        resp->intfs[n].padding  = 0;
    }
    resp->speed = info.speed ? 0x02000000 : 0x01000000;
    resp->idVendor = dev_desc->idVendor;
    resp->idProduct = dev_desc->idProduct;
    resp->bcdDevice = dev_desc->bcdDevice;
    resp->bDeviceClass = dev_desc->bDeviceClass;
    resp->bDeviceSubClass = dev_desc->bDeviceSubClass;
    resp->bDeviceProtocol = dev_desc->bDeviceProtocol;
    resp->bConfigurationValue = config_desc->bConfigurationValue;
    resp->bNumConfigurations = dev_desc->bNumConfigurations;
    resp->bNumInterfaces = config_desc->bNumInterfaces;
}

int USBipDevice::req_ctrl_xfer(usbip_submit_t* req)
{
    // ESP_LOG_BUFFER_HEX_LEVEL("ctrl ep", req, 48, ESP_LOG_ERROR);
    usb_transfer_t* _xfer_ctrl = allocate(1000);
    _xfer_ctrl->callback = usb_ctrl_cb;
    _xfer_ctrl->context = req;
    _xfer_ctrl->bEndpointAddress = __bswap_32(req->header.ep) | (__bswap_32(req->header.direction) << 7);
    // printf("req_ep_xfer ep: %d[%d], dir: %d\n", __bswap_32(req->header.ep), _xfer_ctrl->bEndpointAddress, __bswap_32(req->header.direction));


    usb_setup_packet_t * temp = (usb_setup_packet_t *)_xfer_ctrl->data_buffer;
    size_t n = 0;
    if (req->header.direction == 0) // 0: USBIP_DIR_OUT
    {
        n = __bswap_32(req->length);
        memcpy(_xfer_ctrl->data_buffer, (void*)&req->transfer_buffer, n);
    }
    int _n = __bswap_32(req->length);
    memcpy(temp->val, (uint8_t*)&req->setup, 8 + n);
    // TODO: transfer_buffer. If direction is USBIP_DIR_OUT then n equals transfer_buffer_length; 
    // otherwise n equals 0. 
    // For ISO transfers the padding between each ISO packets is not transmitted.
    _xfer_ctrl->num_bytes = sizeof(usb_setup_packet_t) + __bswap_32(req->length);
    _xfer_ctrl->bEndpointAddress = __bswap_32(req->header.ep) | (__bswap_32(req->header.direction) << 7);
    // ESP_LOG_BUFFER_HEX("", temp->val, len - 40);
    // printf("num_bytes_ctrl[%d/%d]: %d\n", n, _n, _xfer_ctrl->num_bytes);
    _xfer_ctrl->context = req;
    
    esp_err_t err = usb_host_transfer_submit_control(_host->clientHandle(), _xfer_ctrl);

    return  n;
}

// TODO:
int errno;
int USBipDevice::req_ep_xfer(usbip_submit_t* req)
{
    size_t _len = __bswap_32(req->length);
    // ESP_LOG_BUFFER_HEX_LEVEL("xfer ep", req, 48, ESP_LOG_ERROR);

    uint16_t mps = 64;

    if (req->header.direction != 0)
    {
        uint8_t adr = __bswap_32(req->header.ep);
        const usb_ep_desc_t *ep = endpoints[adr][1];
        if (ep)
        {
            mps = ep->wMaxPacketSize;
        } else {
            ESP_LOGE("", "missing EP%d\n", adr);
            return 0;
        }

        _len = usb_round_up_to_mps(_len, mps);
        // ESP_LOGE("", "mps: %d[%d]\n", mps, _len);
    }
    else
    {
        ESP_LOG_BUFFER_HEX("", req, _len);
    }

    usb_transfer_t *xfer_read = allocate(_len);
    if(xfer_read == NULL) return 0;
    xfer_read->callback = &usb_read_cb;
    xfer_read->context = req;
    xfer_read->bEndpointAddress = __bswap_32(req->header.ep);
    // printf("req_ep_xfer ep: %d[%d], dir: %d\n", __bswap_32(req->header.ep), xfer_read->bEndpointAddress, __bswap_32(req->header.direction));
    ESP_LOG_BUFFER_HEX_LEVEL("", req, 48, ESP_LOG_WARN);

    int n = 0;
    if (req->header.direction == 0)
    {
        memcpy(xfer_read->data_buffer, (void*)&req->transfer_buffer, _len);
        n = _len;
    }
    int _n = _len;

    // TODO: transfer_buffer. If direction is USBIP_DIR_OUT then n equals transfer_buffer_length; 
    // otherwise n equals 0. 
    // For ISO transfers the padding between each ISO packets is not transmitted.
    xfer_read->num_bytes = _len;
    // if(xfer_read->num_bytes == 0x100 && req->header.direction != 0) xfer_read->num_bytes = 0xff;
    xfer_read->bEndpointAddress = __bswap_32(req->header.ep) | (__bswap_32(req->header.direction) << 7);
    // ESP_LOG_BUFFER_HEX("", temp->val, len - 40);
    // printf("num_bytes_epx[%d/%d]: %d\n", n, _n, xfer_read->num_bytes);
    xfer_read->context = req;

    esp_err_t err = usb_host_transfer_submit(xfer_read);

    return n;
}

extern "C" void parse_request(const int sock, uint8_t* rx_buffer, size_t len)
{
    uint32_t cmd = __bswap_16(((usbip_request_t*)rx_buffer)->command);
    // printf("cmd: 0x%04x\n", cmd);
    _sock = sock;

    switch (cmd)
    {
    case OP_REQ_DEVLIST:{
        ESP_LOGI(TAG, "OP_REQ_DEVLIST");
        esp_event_post_to(loop_handle, USBIP_EVENT_BASE, OP_REQ_DEVLIST, NULL, 0, 10);
        break;
    }
    case OP_REQ_IMPORT:{
        ESP_LOGI(TAG, "OP_REQ_IMPORT");
        esp_event_post_to(loop_handle, USBIP_EVENT_BASE, OP_REQ_IMPORT, NULL, 0, 10);
        break;
    }
    case USBIP_CMD_SUBMIT:{
        ESP_LOGI(TAG, "USBIP_CMD_SUBMIT");
        urb_data_t data = {
             .len = (int)len,
             .rx_buffer = rx_buffer
        };
        esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_SUBMIT, &data, len + sizeof(int), 10);
        if(xSemaphoreTake(usb_sem, 10000) != pdTRUE) printf("faileeeed\n\n\n");
        xSemaphoreGive(usb_sem);
        break;
    }
    case USBIP_CMD_UNLINK:{
        ESP_LOGI(TAG, "USBIP_CMD_UNLINK");
        usbip_submit_t* _req = (usbip_submit_t*)(rx_buffer);
        usbip_submit_t* req = new usbip_submit_t(); // make it heap caps malloc
        memcpy(req, _req, 0x30);
        esp_event_post_to(loop_handle, USBIP_EVENT_BASE, USBIP_CMD_UNLINK, &req, sizeof(usbip_submit_t*), 10);
        break;
    }
    default:
        ESP_LOGE(TAG, "unknown command: %" PRIu32, cmd); // PRIu32
        break;
    }
    vTaskDelay(10);
}
