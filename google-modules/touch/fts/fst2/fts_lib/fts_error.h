/*
  *
  **************************************************************************
  **                        STMicroelectronics				  **
  **************************************************************************
  *                                                                        *
  *                  FTS error/info kernel log reporting		   *
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */

/*!
  * \file ftsError.h
  * \brief Contains all the definitions and structs which refer to Error
  * conditions
  */

#ifndef FTS_ERROR_H
#define FTS_ERROR_H

/** @defgroup error_codes Error Codes
  * Error codes that can be reported by the driver functions.
  * An error code is made up by 4 bytes, each byte indicate a logic error
  * level.\n
  * From the LSB to the MSB, the logic level increase going from a low level
  * error (I2C,TIMEOUT) to an high level error (flashing procedure fail,
  * production test fail etc)
  * @{
  */

/* FIRST LEVEL ERROR CODE */
/** @defgroup first_level	First Level Error Code
  * @ingroup error_codes
  * Errors related to low level operation which are not under control of driver,
  * such as: communication protocol (I2C/SPI), timeout, file operations ...
  * @{
  */
#define OK				((int)0x00000000) /* /< No ERROR */
#define ERROR_ALLOC			((int)0x80000001) /* /< allocation of
							  * memory failed */
#define ERROR_BUS_R			((int)0x80000002) /* /< i2c/spi read
							  * failed */
#define ERROR_BUS_W			((int)0x80000003) /* /< i2c/spi write
							  * failed */
#define ERROR_BUS_WR			((int)0x80000004) /* /< i2c/spi write/
							  * read failed */
#define ERROR_BUS_O			((int)0x80000005) /* /< error during
							  * opening an i2c
							  * device */
#define ERROR_OP_NOT_ALLOW		((int)0x80000006) /* /< operation not
							 * allowed */
#define ERROR_TIMEOUT			((int)0x80000007) /* /< timeout expired!
							 * exceed the max number
							 * of retries or the max
							 * waiting time */
#define ERROR_FILE_NOT_FOUND		((int)0x80000008) /* /< the file that i
							 * want to open is not
							 * found */
#define ERROR_FILE_PARSE		((int)0x80000009) /* /< error during
							 * parsing the file */
#define ERROR_FILE_READ			((int)0x8000000A)/* /< error during
							 * reading the file */
#define ERROR_LABEL_NOT_FOUND		((int)0x8000000B)/* /<label not found */
#define ERROR_FW_NO_UPDATE		((int)0x8000000C) /* /< fw in the chip
							 * newer than the one in
							 * the bin file */
#define ERROR_FLASH_UNKNOWN		((int)0x8000000D) /* /< flash status
							 * busy or unknown */
#define ERROR_FLASH_CODE_UPDATE		((int)0x8000000E) /*/< error updating
							*fw in flash code part*/
#define ERROR_FLASH_SEC_UPDATE		((int)0x8000000F) /*/< error updating
							*flash section*/
#define ERROR_FLASH_UPDATE		((int)0x80000010) /*/< error  after any
							*flash update*/
#define ERROR_INIT			((int)0x80000011)
/*/< error in Full Panel Intilisation includes auto tune*/
#define ERROR_WRONG_CHIP_ID		((int)0x80000012) /*/<chip id mismatch*/
#define ERROR_CH_LEN			((int)0x80000013) /* /< unable to read
							* the force and/or
							* sense length */
#define ERROR_PROD_TEST_CHECK_FAIL		((int)0x80000014)
/*/<production test limits check  failure*/

/** @}*/

/* SECOND LEVEL ERROR CODE */
/** @defgroup second_level Second Level Error Code
  * @ingroup error_codes
  * Errors related to simple logic operations in the IC which require one
  * command or which are part of a more complex procedure
  * @{
  */
#define ERROR_GET_FRAME_DATA		((int)0x80000100)	/* /< unable to
							 *  retrieve the data of
							 *  a required frame */
#define ERROR_WRONG_DATA_SIGN		((int)0x80000200) /* /< the signature of
							 * the host data is not
							 * HEADER_SIGNATURE */
#define ERROR_SYSTEM_RESET_FAIL		((int)0x80000300)	/* /< the comand
								 * SYSTEM RESET
								 * failed */
#define ERROR_FLASH_NOT_READY		((int)0x80000400)	/* /< flash
								 * status not
								 * ready within
								 * a timeout */


/** @}*/

/* THIRD LEVEL ERROR CODE */
/** @defgroup third_level	Third Level Error Code
  * @ingroup error_codes
  * Errors related to logic operations in the IC which require more
  *commands/steps or which are part of a more complex procedure
  * @{
  */

#define ERROR_GET_FRAME			((int)0x80010000) /*/<frame read error*/
#define ERROR_GET_CX			((int)0x80020000)/*/<cx data read err*/
#define ERROR_MEMH_READ			((int)0x80030000)
/* /< memh reading failed */



/** @}*/

/* FOURTH LEVEL ERROR CODE */
/** @defgroup fourth_level	Fourth Level Error Code
  * @ingroup error_codes
  * Errors related to the highest logic operations in the IC which have an
  * important impact on the driver flow or which require several commands and
  * steps to be executed
  * @{
  */


#define ERROR_FLASH_PROCEDURE		((int)0x81000000)	/* /< fw update
							 * procedure failed */
#define ERROR_PROD_TEST_ITO		((int)0x82000000)	/* /< production
							 *  ito test failed */
#define ERROR_PROD_TEST_RAW		((int)0x83000000)	/* /< production
							*raw data test failed */
#define ERROR_PROD_TEST_CX		((int)0x84000000) /* /< production
							*test cx data failed */

/** @}*/

/** @}*/	/* end of error_commands section */

/* ERROR TYPE */
/** @defgroup error_type  Error Event Types
  * @ingroup events_group
  * Types of EVT_ID_ERROR events reported by the FW
  * @{
  */

#define EVT_TYPE_ERROR_HARD_FAULT	         0x01/* /< Hard Fault */
#define EVT_TYPE_ERROR_MEMORY_MANAGE	         0x02/* /< Memory Mange */
#define EVT_TYPE_ERROR_BUS_FAULT		 0x03/* /< Bus Fault */
#define EVT_TYPE_ERROR_USAGE_FAULT		 0x04/* /< Usage Fault */
#define EVT_TYPE_ERROR_WATCHDOG			 0x05/* /< Watchdog timer
							* expired  */
#define EVT_TYPE_ERROR_ITO_FORCETOGND		 0x60	/* /< Force channel/s
						 * short to ground */
#define EVT_TYPE_ERROR_ITO_SENSETOGND		 0x61	/* /< Sense channel/s
						 * short to ground */
#define EVT_TYPE_ERROR_ITO_FLTTOGND		 0x62	/* /< Flt short to
							* ground */
#define EVT_TYPE_ERROR_ITO_FORCETOVDD		 0x63	/* /< Force channel/s
						 * short to VDD */
#define EVT_TYPE_ERROR_ITO_SENSETOVDD		 0x64	/* /< Sense channel/s
						 * short to VDD */
#define EVT_TYPE_ERROR_ITO_FLTTOVDD		 0x65
#define EVT_TYPE_ERROR_ITO_FORCE_P2P		 0x66/*/< Pin to Pin short Force
						 * channel/s */
#define EVT_TYPE_ERROR_ITO_SENSE_P2P		 0x67 /* /< Pin to Pin short
						 * Sense channel/s */
#define EVT_TYPE_ERROR_ITO_FLT_P2P		 0x68 /*/<Flt short pin to pin*/
#define EVT_TYPE_ERROR_ITO_FORCEOPEN		 0x69 /* /< Force Panel open */
#define EVT_TYPE_ERROR_ITO_SENSEOPEN		 0x6A /* /< Sense Panel open */
/** @}*/

/* TIMEOUT */
/** @defgroup timeouts	 Timeouts
  * Definitions of all the Timeout used in several operations
  * @{
  */

#define TIMEOUT_RESOLUTION			 10
/* /< timeout resolution in ms (all timeout should be multiples of this unit) */
#define TIMEOUT_FW_REG_STATUS			 300
/* /< timeout of the FW Register request status */
#define TIMEOUT_GENERAL				 100
/* /< general timeout in ms */
#define TIMEOUT_FPI				 5000
/* /< timeout of the Full panel Init command */

/** @}*/

#endif
