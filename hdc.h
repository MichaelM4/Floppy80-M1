
#define MAX_VHD_HEADS 4
#define MAX_VHD_CYLINDERS 1024
#define MAX_VHD_SECTORS_PER_CYLINDER 32

void HdcInit(void);
void HdcServiceStateMachine(void);

void hdc_port_out(word addr, byte data);
byte hdc_port_in(word addr);
