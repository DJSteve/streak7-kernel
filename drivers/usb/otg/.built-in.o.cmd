cmd_drivers/usb/otg/built-in.o :=  arm-eabi-ld -EL    -r -o drivers/usb/otg/built-in.o drivers/usb/otg/otg.o drivers/usb/otg/gpio_vbus.o drivers/usb/otg/tegra-otg.o drivers/usb/otg/nop-usb-xceiv.o drivers/usb/otg/ulpi.o drivers/usb/otg/ulpi_viewport.o 
