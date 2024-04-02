/*
 * canframe.h
 *
 * Created: 1/13/2018 1:32:28 PM
 *  Author: Dick
 */
#ifndef CANFRAME_H_
#define CANFRAME_H_

#if defined(AVR) || defined(_WINNT_) || defined(_WIN32)

 /* special address description flags for the CAN_ID */
#define CAN_EFF_FLAG 0x80000000U /* EFF/SFF is set in the MSB */
#define CAN_RTR_FLAG 0x40000000U /* remote transmission request */
#define CAN_ERR_FLAG 0x20000000U /* error frame */

/* valid bits in CAN ID for frame formats */
#define CAN_SFF_MASK 0x000007FFU /* standard frame format (SFF) */
#define CAN_EFF_MASK 0x1FFFFFFFU /* extended frame format (EFF) */
#define CAN_ERR_MASK 0x1FFFFFFFU /* omit EFF, RTR, ERR flags */

typedef struct can_frame can_frame_t;

/**
 * struct can_frame - basic CAN frame structure
 * @can_id:  CAN ID of the frame and CAN_*_FLAG flags, see canid_t definition
 * @can_dlc: frame payload length in byte (0 .. 8) aka data length code
 *           N.B. the DLC field from ISO 11898-1 Chapter 8.4.2.3 has a 1:1
 *           mapping of the 'data length code' to the real payload length
 * @__pad:   padding
 * @__res0:  reserved / padding
 * @__res1:  reserved / padding
 * @data:    CAN frame payload (up to 8 byte)
 */
#define CAN_MAX_DLEN		8

struct can_frame{
	uint32_t	can_id;  /* 32 bit CAN_ID + EFF/RTR/ERR flags */
	uint8_t		can_dlc; /* frame payload length in byte (0 .. CAN_MAX_DLEN) */
	uint8_t		data[CAN_MAX_DLEN];
};

#else

#include <linux/can.h>

#endif /* AVR */

#endif /* CANFRAME_H_ */