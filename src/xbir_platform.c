/******************************************************************************
* Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/

/*****************************************************************************/
/**
*
* @file xbir_platform.c
*
* This file contains platform specific API's for System Board.
*
******************************************************************************/

/***************************** Include Files *********************************/
#include "xparameters.h"
#include "xparameters_ps.h"	/* Defines XPAR values */
#include "xscugic.h"
#include "lwip/tcp.h"
#include "xbir_config.h"
#include "netif/xadapter.h"
#include "xbir_platform.h"
#include "sleep.h"

/************************** Constant Definitions *****************************/
#define INTC_DEVICE_ID		XPAR_SCUGIC_SINGLE_DEVICE_ID
#define TIMER_IRPT_INTR		XPAR_XTTCPS_0_INTR
#define PLATFORM_TIMER_INTR_RATE_HZ	(4U)

#ifndef SDT
#define TIMER_DEVICE_ID		XPAR_XTTCPS_0_DEVICE_ID
#define INTC_BASE_ADDR		XPAR_SCUGIC_0_CPU_BASEADDR
#define INTC_DIST_BASE_ADDR	XPAR_SCUGIC_0_DIST_BASEADDR
#else
#define TIMER_DEVICE_ID		XPAR_XTTCPS_0_BASEADDR
// change the INTC_BASE_ADDR to canonical format
#define INTC_BASE_ADDR		XPAR_GIC_BASEADDR_1
#define INTC_DIST_BASE_ADDR	XPAR_XSCUGIC_0_BASEADDR
#endif

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/
static XTtcPs TimerInstance = {0U};
static XScuGic GicInstance = {0U};
volatile u8 TcpFastTmrFlag = FALSE;
volatile u8 TcpSlowTmrFlag = FALSE;

#if LWIP_DHCP==1
void dhcp_fine_tmr();
void dhcp_coarse_tmr();
#endif

/*****************************************************************************/
/**
 * @brief
 * This function is callback function for handling timer interrupts.
 *
 * @param	TimerInstance	Pointer to instance of XTtcPs
 *
 * @return	None
 *
 *****************************************************************************/
void Xbir_Platform_TimerCallback (void)
{
	/* We need to call tcp_fasttmr & tcp_slowtmr at intervals specified
	 * by lwIP. It is not important that the timing is absoluetly accurate.
	 */
	static u8 Odd = 1U;
#if LWIP_DHCP==1
	static int dhcp_timer = 0U;
#endif

	TcpFastTmrFlag = TRUE;
	Odd = !Odd;
	if (Odd > 0U) {
		TcpSlowTmrFlag = 1U;
#if LWIP_DHCP==1
		dhcp_timer++;
		(void)Xbir_dhcp_timoutcntr(DEC);
		dhcp_fine_tmr();
		if (dhcp_timer >= DHCP_TIMER_COUNT) {
			dhcp_coarse_tmr();
			dhcp_timer = 0U;
		}
#endif
	}

	Xbir_Platform_ClearInterrupt ();
}

/*****************************************************************************/
/**
 * @brief
 * This function sets up the platform timer required for lwip.
 *
 * @param	None
 *
 * @return	XST_SUCCESS on success
 *		Error code on failure
 *
 *****************************************************************************/
int Xbir_Platform_SetupTimer (void)
{
	int Status = XST_FAILURE;
	XTtcPs *Timer = &TimerInstance;
	XTtcPs_Config *Config;
	XInterval  Interval;
	u8 Prescaler;

	Config = XTtcPs_LookupConfig(TIMER_DEVICE_ID);
	if (Config == NULL) {
		Xbir_Printf(DEBUG_INFO, " In %s: Look up config failed\r\n", __func__);
		goto END;
	}
	Status = XTtcPs_CfgInitialize(Timer, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		Xbir_Printf(DEBUG_INFO, " In %s: Timer Cfg initialization failed...\r\n",
			__func__);
		goto END;
	}

	/* Stop timer and reset counter before configuration */
	XTtcPs_Stop(Timer);
	XTtcPs_ResetCounterValue(Timer);

	/* Clear any pending interrupts */
	u32 StatusEvent = XTtcPs_GetInterruptStatus(Timer);
	XTtcPs_ClearInterruptStatus(Timer, StatusEvent);

	XTtcPs_SetOptions(Timer,
		XTTCPS_OPTION_INTERVAL_MODE | XTTCPS_OPTION_WAVE_DISABLE);

	/* Manual calculation for 4 Hz (250ms) interrupt rate:
	 * Clock = 100 MHz, Target = 4 Hz (250ms)
	 * For best accuracy with 4Hz: use prescaler 7 (divide by 256)
	 * Effective clock = 100MHz / 256 = 390625 Hz
	 * For 4 Hz: 390625 / 4 = 97656.25 counts per interrupt
	 */
	Prescaler = 7;  /* Divide by 2^(7+1) = 256 */
	Interval = 97656; /* 390625 Hz / 4 Hz */

	XTtcPs_SetInterval(Timer, Interval);
	XTtcPs_SetPrescaler(Timer, Prescaler);

END:
	return Status;
}

/*****************************************************************************/
/**
 * @brief
 * This function clears the timer interrupt.
 *
 * @param	None
 *
 * @return	None
 *
 *****************************************************************************/
void Xbir_Platform_ClearInterrupt (void)
{
	u32 StatusEvent;

	StatusEvent = XTtcPs_GetInterruptStatus(&TimerInstance);
	XTtcPs_ClearInterruptStatus(&TimerInstance, StatusEvent);
	(void) StatusEvent;
}

/*****************************************************************************/
/**
 * @brief
 * This function registers interrupt handler for platform interrupts.
 *
 * @param	None
 *
 * @return	None
 *
 *****************************************************************************/
void Xbir_Platform_SetupInterrupts (void)
{
	int Status;
	XScuGic_Config *GicConfig;

	/* Initialize exception system */
	Xil_ExceptionInit();

	/* Lookup GIC configuration */
	GicConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (GicConfig == NULL) {
		Xbir_Printf(DEBUG_INFO, "ERROR: GIC LookupConfig failed!\n\r");
		return;
	}

	/* Initialize the GIC */
	Status = XScuGic_CfgInitialize(&GicInstance, GicConfig, GicConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		Xbir_Printf(DEBUG_INFO, "ERROR: GIC CfgInitialize failed: %d\n\r", Status);
		return;
	}

	/*
	 * Connect the interrupt controller interrupt handler to the hardware
	 * interrupt handling logic in the processor.
	 */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
		(Xil_ExceptionHandler)XScuGic_InterruptHandler,
		&GicInstance);

	/*
	 * Connect the timer interrupt handler
	 */
	Status = XScuGic_Connect(&GicInstance, TIMER_IRPT_INTR,
		(Xil_ExceptionHandler)Xbir_Platform_TimerCallback,
		(void *)&TimerInstance);
	if (Status != XST_SUCCESS) {
		Xbir_Printf(DEBUG_INFO, "ERROR: GIC Connect failed: %d\n\r", Status);
		return;
	}

	/*
	 * Enable the IRQ exception
	 */
	Xil_ExceptionEnable();

	/* Enable the timer interrupt in the GIC */
	XScuGic_SetPriorityTriggerType(&GicInstance, TIMER_IRPT_INTR, 0xA0, 0x3);
	XScuGic_Enable(&GicInstance, TIMER_IRPT_INTR);
}

/*****************************************************************************/
/**
 * @brief
 * This function enables required platform interrupts.
 *
 * @param	None
 *
 * @return	None
 *
 *****************************************************************************/
void Xbir_Platform_EnableInterrupts (void)
{
	/* Ensure timer is stopped before enabling interrupts */
	XTtcPs_Stop(&TimerInstance);

	/* Clear any pending interrupts before enabling */
	u32 PendingIntr = XTtcPs_GetInterruptStatus(&TimerInstance);
	XTtcPs_ClearInterruptStatus(&TimerInstance, PendingIntr);

	/* Enable timer interrupts */
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);
	XTtcPs_EnableInterrupts(&TimerInstance, XTTCPS_IXR_INTERVAL_MASK);

	/* Reset counter to 0 and start */
	XTtcPs_ResetCounterValue(&TimerInstance);
	XTtcPs_Start(&TimerInstance);
}

/*****************************************************************************/
/**
 * @brief
 * This function initialize platform to run this application.
 *
 * @param	None
 *
 * @return	XST_SUCCESS on success
 *		Error code on failure
 *
 *****************************************************************************/
int Xbir_Platform_Init (void)
{
	int Status = XST_FAILURE;

	Status = Xbir_Platform_SetupTimer();
	if (Status == XST_SUCCESS) {
		Xbir_Platform_SetupInterrupts();
	}

	return Status;
}

#if LWIP_DHCP==1
/*****************************************************************************/
/**
 * @brief
 * This function handles dhcp_timoutcntr variable
 *
 * @param	state variable takes INIT, GET, DEC to manipulate the variable
 *
 * @return	dhcp_timoutcntr value
 *
 *****************************************************************************/
int Xbir_dhcp_timoutcntr(int state)
{
	static volatile int dhcp_timoutcntr = DHCP_TIMEOUT;

	if (state == INIT) {
		dhcp_timoutcntr = DHCP_TIMEOUT;
	} else if ((state == DEC) && (dhcp_timoutcntr > 0)) {
		dhcp_timoutcntr --;
	}

	return dhcp_timoutcntr;
}

#endif
