
int serial_debug_serial_printf(char *format, ...);

int _start()
{
    serial_debug_serial_printf("Jump To Kernel Was Successful!\n");
    while (1);
}
