idf_component_register(SRCS 
    "host/usb_host.cpp" 
    "host/usb_device.cpp"

    INCLUDE_DIRS "include"

    PRIV_REQUIRES usb
)
