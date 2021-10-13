#ifndef TLV320AIC3104_H
#define TLV320AIC3104_H

#include "driver/i2c.h"

int tlv320aic3104_init(i2c_port_t port, uint8_t i2c_address);

#endif // TLV320AIC3104_H