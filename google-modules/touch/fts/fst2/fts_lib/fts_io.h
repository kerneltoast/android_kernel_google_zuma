/*
  *
  **************************************************************************
  **                        STMicroelectronics				**
  **************************************************************************
  *                                                                        *
  *                     I2C/SPI Communication				*
  *                                                                        *
  **************************************************************************
  **************************************************************************
  *
  */
/*!
  * \file ftsIO.h
  * \brief Contains all the definitions and prototypes used and implemented in
  * ftsIO.c
  */

#ifndef _LINUX_FTS_IO_H_
#define _LINUX_FTS_IO_H_

/*#define I2C_INTERFACE*/
#ifdef I2C_INTERFACE
#define I2C_SAD				0x49	/* /< slave address of the IC */
#define DUMMY_BYTE			0	/* /< first byte read is not
						* Dummy byte  */
#else
#define SPI4_WIRE	/* /< comment if the master is SPI3 wires
			 *  (MOSI and MISO share same line) */
#define SPI_DELAY_CS			10	/* /< time in usec to wait
						 * before rising the CS */
#define DUMMY_BYTE			1 /* /< first byte read is Dummy byte */
#endif

/* CHIP INFO */
/** @defgroup system_info	System Info
  * System Info Data collect the most important informations about hw and fw
  * @{
  */
#define SYS_INFO_SIZE			256 /* Size in bytes of System Info data */
#define DIE_INFO_SIZE			16 /* Num bytes of die info */
#define RELEASE_INFO_SIZE		8 /* Num bytes of external release
						 * in config */
/** @}*/


#define FTS_CMD_HDM_SPI_W		0xB6 /* /< command to write a HDM
					 * request if FTI */
#define FTS_CMD_HDM_SPI_R		0xB7 /* /< command to read a HDM
					 * request if FTI */
#define FTS_CMD_REG_SPI_W		0xB2 /* /< command to write  fw
					 * register if FTI */
#define FTS_CMD_REG_SPI_R		0xB1 /* /< command to read fw register
					 * if FTI */
#define FTS_CMD_NONE			0x00
#define FTS_CMD_HW_REG_W		0xFA /* /< command to write an hw
					 * register if FTI */

#ifdef I2C_INTERFACE
#define FTS_CMD_HW_REG_R		0xFA /* /< command to read an hw
					 * register if FTI */
#define FRAME_BUFFER_ADDR		0x8000
#define READ_CHUNK			1024 /* /< chunk dimension of
						  * a single i2c read,
						  * max allowed value is 2kB */
#define WRITE_CHUNK			1024	/* /< chunk dimension of
						  * a single i2c write,
						  * max allowed value is 2kB */
#define MEMORY_CHUNK			1024	/* /< chunk dimenasion of
						  * a single i2c write on mem,
						  * max allowed value is 2kB */
#else
#define FTS_CMD_HW_REG_R		0xFB /* /< command to read an hw
					 * register if FTI */
#define FRAME_BUFFER_ADDR		0x0000 /* /< frame buffer
							 * address in memory */
#define SPI_REG_W_CHUNK			128 /* /< chunk dimension of a
						  * single SPI register write*/
#define SPI_REG_R_CHUNK			1024 /* /< chunk dimension of
						  * a single SPI register read*/
#define SPI_HDM_W_CHUNK			1024 /* /< chunk dimension of
						  * a single SPI HDM write*/
#define SPI_HDM_R_CHUNK			1024 /* /< chunk dimension of
						  * a single SPI HDM read*/
#define READ_CHUNK			SPI_HDM_R_CHUNK /* /< chunk dimension of
						  * a single SPI  read*/
#define WRITE_CHUNK			SPI_HDM_W_CHUNK /* /< chunk dimension of
						  * a single SPI wrtite*/
#define MEMORY_CHUNK			SPI_HDM_R_CHUNK /* /< chunk dimenasion
						  * of single spi write on mem*/
#endif
#define I2C_RETRY			3 /* /< number of retry in case of i2c
					 * failure */
#define I2C_WAIT_BEFORE_RETRY		2	/* /< wait in ms before retry an
					 * i2c transaction */
#define GPIO_NOT_DEFINED		-1  /* /< value assumed by reset_gpio
					 * when the reset pin of the IC is not
					 * connected */


#define FW_ADDR_SIZE			BITS_16 /* /< value of AddrSize for Fw
					 * register in FTI @see AddrSize */
#define HW_ADDR_SIZE			BITS_32 /* /< value of AddrSize for Hw
					 * register in FTI @see AddrSize */

#define CHIP_ID_ADDRESS			0x20000000 /* /< chip id address
							 * for FTI */
#define UVLO_CTRL_ADDR			0x2000001B/* /< uvlo control address
							 * for FTI */
#define SYS_RST_ADDR			0x20000024 /* /< address of
							 * System control reg
							 * in FTI */
#define BOOT_OPT_ADDR			0x20000025/* /< boot option  address
							 * for FTI */
#define SPI4_CONFIG_ADDR		0x2000002D
#define GPIO_GPIO_PU_ADDR		0x20000034/* /< address of GPIO
							 * pullup register */
#define GPIO_MISO_CONFIG_ADDR		0x2000003E/* /< address of GPIO
							 *MISO config*/
#define FLASH_FSM_CTRL_ADDR		0x20000068

#define FLASH_ERASE_CTRL_ADDR		0x2000006A /* /< flash start address
							 * for FTI */
#define PAGE_SEL_ADDR			0x2000006B/* /< flash page select
							 * address for FTI */
#define FLASH_MULTI_PAGE_ERASE_ADDR	0x2000006E/* /< flash multi page Erase
							 * address for FTI */
#define FLASH_DMA_ADDR			0x20000072/* /< flash DMA address
							 * for FTI */
#define CODE_STATUS_ADDR		0x20000078/* /< Hw reg code status
							 * address for FTI */
#define FLASH_CTRL_ADDR			0x200000DE/* /< flash control  address
							 * for FTI */
#define FLASH_PAGE_MASK_ADDR		0x20000128/* /< flash page mask address
							 * for FTI */

#define FRAME_BUFFER_ADDRESS		0x20010000/* /< frame buffer
							 * address in memory */
#define FLASH_START_ADDR		0x00000000 /* /< flash start address
							 * for FTI */

#define SCAN_MODE_ADDR			0x0010 /* /< FW reg scan mode
							 * address */
#define FLASH_SAVE_ADDR			0x0020 /* /< FW reg flash save
							 * address */
#define HDM_WRITE_REQ_ADDR		0x0021 /* /< FW reg hdm write
							 * address */
#define PI_ADDR				0x0022  /* /< FW reg Panel Init
							 * address */
#define HDM_REQ_ADDR			0x0023 /* /< FW reg HDM request
							 * address */
#define ITO_TRIGGER_ADDR		0x0024 /* /< FW reg ITO trigger
							 * address */
#define SYS_ERROR_ADDR			0x0040 /* /< FW reg system error
							 * status  address */
#define FIFO_READ_ADDR			0x0060 /* /< FW reg FIFO read
							 * address */

/* EVENT ID */
/** @defgroup events_group	 FW Event IDs and Types
  * Event IDs and Types pushed by the FW into the FIFO
  * @{
  */
#define EVT_ID_NOEVENT			0x00	/* /< No Events */
#define EVT_ID_CONTROLLER_READY		0x03	/* /< Controller ready, issued
						* after a system reset. */
#define EVT_ID_ENTER_POINT		0x13	/* /< Touch enter in the
						* sensing area */
#define EVT_ID_MOTION_POINT		0x23	/* /< Touch motion (a specific
						* touch changed position) */
#define EVT_ID_LEAVE_POINT		0x33	/* /< Touch leave the sensing
						* area */
#define EVT_ID_USER_REPORT		0x53	/* /< User related events
						* triggered (keys,
						* gestures, proximity etc) */
#define EVT_ID_DEBUG			0xE3	/* /< Debug Info */
#define EVT_ID_ERROR			0xF3	/* /< Error Event */

#define EVT_ID_ENTER_PEN		0x73	/* /< Pen enter in the
						* sensing area */
#define EVT_ID_MOTION_PEN		0x83	/* /< Pen motion (a specific
						* touch changed position) */
#define EVT_ID_LEAVE_PEN		0x93	/* /< Pen leave the sensing
						* area */


#define FIFO_EVENT_SIZE			8 /* /< number of bytes of one event */
#define NUM_EVT_ID			(((EVT_ID_ERROR & 0xF0) >> 4)+1)
/* /< Max number of unique event IDs supported */
/** @}*/

#define BYTES_PER_NODE			2 /* /< number of data bytes for each
						* node */
#define SYNC_FRAME_HEADER_SIZE		16/* /< number of bytes of
						* Sync Frame Header */
#define COMP_HEADER_SIZE		16/* /< number of bytes of
						* Compensation  Frame Header */

#define HDM_REQ_SYS_INFO		0x01 /* /< Load HDM System Info */
#define HDM_REQ_CX_MS_TOUCH		0x10 /* /< Load HDM MS Init Data for
						* Active Mode */
#define HDM_REQ_CX_MS_LOW_POWER		0x11 /* /< Load HDM MS Init Data for
						* Low power Mode */
#define HDM_REQ_CX_SS_TOUCH		0x12 /* /< Load HDM SS Init Data for
						* Active  */
#define HDM_REQ_CX_SS_TOUCH_IDLE	0x13 /* /< Load HDM SS Init Data for
						* Low power Mode */
#define HDM_REQ_TOT_CX_MS_TOUCH		0x50 /* /< Load HDM TOT MS Init
						* Data for Active  */
#define HDM_REQ_TOT_CX_MS_LOW_POWER	0x51 /* /< Load HDM TOT MS Init Data
						*  for Low power Mode */
#define HDM_REQ_TOT_IX_SS_TOUCH		0x52 /* /< Load HDM TOT SS Init
						* Data for Active  */
#define HDM_REQ_TOT_IX_SS_TOUCH_IDLE	0x53 /* /< Load HDM TOT SS Init Data
						 *  for Low power Mode */


#define SYSTEM_RESET_VAL		0x80 /* /< System reset Value*/
#define SCAN_MODE_HIBERNATE		0x00 /* /< Scan mode Hibernate Value*/
#define SCAN_MODE_ACTIVE		0x01 /* /< Scan mode active Value*/
#define SCAN_MODE_LOW_POWER		0x02 /* /< Scan mode Low Power  Value*/
#define SCAN_MODE_LOCK_ACTIVE		0x10 /* /< Scan mode lock active Value*/
#define SCAN_MODE_LOCK_LP_DETECT	0x13 /* /< Scan mode lock LP detect
						* Value */
#define SCAN_MODE_LOCK_LP_ACTIVE	0x14 /* /<  can mode lock LP Active
						* Value */

/*#define MS_GV_METHOD
#define SS_GV_METHOD*/

typedef signed char i8;
/** @addtogroup system_info
  * @{
  */

/**
  * Struct which contains fundamental informations about the chip and its
  *configuration
  */
struct sys_info {
	u16 u16_api_ver_rev; /* /< API revision version */
	u8 u8_api_ver_minor; /* /< API minor version */
	u8 u8_api_ver_major; /* /< API major version */
	u16 u16_chip0_ver;	/* /< Dev0 version */
	u16 u16_chip0_id;	/* /< Dev0 ID */
	u16 u16_chip1_ver;	/* /< Dev1 version */
	u16 u16_chip1_id;	/* /< Dev1 ID */
	u16 u16_fw_ver;		/* /< Fw version */
	u16 u16_svn_rev;	/* /< SVN Revision */
	u16 u16_pe_ver;		/* /< PE Version */
	u16 u16_reg_ver;	/* /< Config Version */
	u16 u16_scr_x_res;	/* /< screen res in pixels */
	u16 u16_scr_y_res;	/* /< screen res in pixels */
	u8 u8_scr_tx_len;	/* /< screen TX channel count */
	u8 u8_scr_rx_len;	/* /< screen RX channel count */
	u8 u8_die_info[DIE_INFO_SIZE];/* /< Die information */
	u8 u8_release_info[RELEASE_INFO_SIZE];/* /< Release information */
	u32 u32_flash_org_info; /* /< flash organisation*/
	u8 u8_cfg_afe_ver; /* /< AFE version in Config */
	u8 u8_ms_scr_afe_ver; /* /< AFE ver MS screen */
	u8 u8_ms_scr_gv_ver;/* /< GV ver MS screen */
	u8 u8_ms_scr_lp_afe_ver; /* /< AFE ver MS screen  low power*/
	u8 u8_ms_scr_lp_gv_ver;/* /< GV ver MS screen  low power*/
	u8 u8_ss_tch_afe_ver; /* /< AFE ver SS touch */
	u8 u8_ss_tch_gv_ver;	/* /< GV ver SS touch */
	u8 u8_ss_det_afe_ver; /* /< AFE ver SS detect */
	u8 u8_ss_det_gv_ver; /* /< GV ver MS detect */
	u16 u16_dbg_info_addr; /* /< Offset of debug Info structure */
	u16 u16_ms_scr_raw_addr;/* /< Offset of MS touch raw frame */
	u16 u16_ms_scr_filter_addr;/* /< Offset of MS touch filter frame */
	u16 u16_ms_scr_strength_addr;/* /< Offset of MS touch strength frame */
	u16 u16_ms_scr_baseline_addr;/* /< Offset of MS touch baseline frame
					 * */
	u16 u16_ss_tch_tx_raw_addr;/* /< Offset of SS touch force raw frame */
	u16 u16_ss_tch_tx_filter_addr;/* /< Offset of SS touch force filter
					 * strength frame */
	u16 u16_ss_tch_tx_strength_addr;	/* /< Offset of SS touch force
					 * frame */
	u16 u16_ss_tch_tx_baseline_addr;	/* /< Offset of SS touch force
					 * baseline frame */
	u16 u16_ss_tch_rx_raw_addr; /* /< Offset of SS touch sense raw frame */
	u16 u16_ss_tch_rx_filter_addr;	/* /< Offset of SS touch sense filter
					 * frame */
	u16 u16_ss_tch_rx_strength_addr;	/* /< Offset of SS touch sense
					 * strength frame */
	u16 u16_ss_tch_rx_baseline_addr;	/* /< Offset of SS touch sense
					 * baseline frame */
	u16 u16_ss_det_tx_raw_addr;/* /< Offset of SS detect tx raw frame */
	u16 u16_ss_det_tx_filter_addr;/* /< Offset of SS detect tx filter
					 * frame */
	u16 u16_ss_det_tx_strength_addr;/* /< Offset of SS detect tx strength
					 * frame */
	u16 u16_ss_det_tx_baseline_addr;/* /< Offset of SS detect tx baseline
					 * frame */
	u16 u16_ss_det_rx_raw_addr; /* /< Offset of SS detect rx raw frame */
	u16 u16_ss_det_rx_filter_addr;/* /< Offset of SS detect rx filter
					 * frame */
	u16 u16_ss_det_rx_strength_addr;/* /< Offset of SS detect rx strength
					 * frame */
	u16 u16_ss_det_rx_baseline_addr;/* /< Offset of SS detect rx baseline
					 * frame */
	u32 u32_reg_default_sect_flash_addr; /* /< default register sector flash
					 * address */
	u32 u32_misc_sect_flash_addr;/* /< misc sector flash
					 * address */
	u32 u32_cx_ms_scr_flash_addr;/* /< MS screen Cx sector flash
					 * address */
	u32 u32_cx_ms_scr_lp_flash_addr;/* /< MS screen LP cx register sector
					 * flash address */
	u32 u32_cx_ss_tch_flash_addr;/* /< SS touch cx sector flash
					 * address */
	u32 u32_cx_ss_det_flash_addr;/* /< SS detect cx sector flash
					 * address */
	u32 u32_ioff_ms_scr_flash_addr;/* /< MS screen ioffset sector flash
					* address */
	u32 u32_ioff_ms_scr_lp_flash_addr;/* /< MS screen ioffset LP sector
					 * flash address */
	u32 u32_ioff_ss_tch_flash_addr; /* /< SS touch  ioffset sector flash
					 * address */
	u32 u32_ioff_ss_det_flash_addr;/* /< SS detect  ioffset sector flash
					 * address */
	u32 u32_pure_raw_ms_scr_flash_addr;/* /< MS screen pure raw sector flash
					 * address */
	u32 u32_pure_raw_ms_scr_lp_flash_addr;/* /< MS screen pure raw LP sector
					 * flash address */
	u32 u32_pure_raw_ss_tch_flash_addr;/* /< SS touch pure raw sector flash
					 * address */
	u32 u32_pure_raw_ss_det_flash_addr;/* /< SS detect pure raw sector flash
					 * address */
};

/** @}*/

/**
  * Possible types of MS frames
  */
typedef enum {
	MS_RAW = 0, /* /< Mutual Sense Raw Frame */
	MS_FILTER = 1,	/* /< Mutual Sense Filtered Frame */
	MS_STRENGTH = 2,	/* /< Mutual Sense Baseline Frame */
	MS_BASELINE = 3,	/* /< Mutual Sense Key Raw Frame */
} ms_frame_type_t;

/**
  * Possible types of SS frames
  */

typedef enum {
	SS_RAW = 0, /* /< Self Sense Raw Frame */
	SS_FILTER = 1,	/* /< Self Sense Filtered Frame */
	SS_STRENGTH = 2, /* /< Self Sense Strength Frame*/
	SS_BASELINE = 3, /* /< Self Sense Baseline Frame */
	SS_DETECT_RAW = 4,/* /< Self Sense Detect Raw Frame */
	SS_DETECT_FILTER = 5,/* /< Self Sense Detect Filter Frame */
	SS_DETECT_STRENGTH = 6,/* /< Self Sense Detect Strength Frame */
	SS_DETECT_BASELINE = 7,/* /< Self Sense Detect Baseline Frame */
} ss_frame_type_t;


/**
  * Struct which contains the general info about Frames and Initialization Data
  */
struct data_header {
	int force_node;	/* /< Number of Force Channels in the
			 * frame/Initialization data */
	int sense_node;	/* /< Number of Sense Channels in the
			 * frame/Initialization data */
	int type;	/* /< Type of frame/Initialization data */
};

/**
  * Struct which contains the MS data info and frame
  */

struct mutual_sense_frame {
	struct data_header header; /* /< Header which contain basic info of the
				 * frame */
	short *node_data; /* /< Data of the frame */
	int node_data_size; /* /< Dimension of the data of the frame */
};

/**
  * Struct which contains the SS data info and frame
  */

struct self_sense_frame {
	struct data_header header; /* /< Header which contain basic info of the
				 * frame */
	short *force_data;	/* /< Force Channels Data */
	short *sense_data;	/* /< Sense Channels Data */
};

/**
  * Struct which contains the MS CX data info and frame
  */

struct mutual_sense_cx_data {
	struct data_header header; /* /< Header which contain basic info of the
				 * frame */
	i8 cx1;/* /< Cx1 value (can be negative)) */
	i8 *node_data;/* /< Data of the frame */
	int node_data_size;/* /< Dimension of the data of the frame */
};

/**
  * Struct which contains the TOT MS Initialization data
  */

struct mutual_total_cx_data {
	struct data_header header; /* /< Header which contain basic info of the
				 * frame */
	short *node_data;/* /< Data of the frame */
	int node_data_size;/* /< Dimension of the data of the frame */
};
/**
  * Struct which contains the SS CX data info and frame
  */

struct self_sense_cx_data {
	struct data_header header; /* /< Header */
	u8 tx_ix0; /* /< SS TX IX0 */
	u8 rx_ix0; /* /< SS RX IX0 */
	u8 tx_ix1; /* /< SS TX IX1 */
	u8 rx_ix1; /* /< SS RX IX1*/
	u8 tx_max_n; /* /< SS TX MaxN */
	u8 rx_max_n; /* /< SS RX MaxN */
	i8 tx_cx1; /* /<  SS TX Cx1 (can be negative)*/
	i8 rx_cx1; /* /< SS RX Cx1 (can be negative)*/
	u8 *ix2_tx; /* /< pointer to an array of bytes which contains Force
			 * Ix2 data node  */
	u8 *ix2_rx; /* /< pointer to an array of bytes which contains Sense
			 * Ix2 data node  */
	i8 *cx2_tx; /* /< pointer to an array of bytes which contains Force
			 * Cx2 data node (can be negative) */
	i8 *cx2_rx; /* /<  pointer to an array of bytes which contains Sense
			 * Cx2 data node (can be negative)) */
};


/**
  * Struct which contains the TOT SS Initialization data
  */
struct self_total_cx_data {
	struct data_header header; /* /< Header */
	u16 *ix_tx; /* /< pointer to an array of ushort which contains TOT
			 * SS IX Force data */
	u16 *ix_rx;/* /< pointer to an array of ushort which contains TOT
			 * SS IX Sense data */
};

/**
  * Possible data sizes
  */

typedef enum {
	NO_ADDR = 0,
	BITS_8 = 1,
	BITS_16 = 2,
	BITS_24 = 3,
	BITS_32 = 4,
	BITS_40 = 5,
	BITS_48 = 6,
	BITS_56 = 7,
	BITS_64 = 8,
} addr_size_t;


#ifdef I2C_INTERFACE
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
struct i2c_client *get_client(void);
#else
#include <linux/spi/spi.h>
struct spi_device *get_client(void);
#endif
struct device *get_dev(void);
void set_reset_gpio(int gpio);
int open_channel(void *clt);
void log_info(int force, const char *msg, ...);
int fts_read(u8 *outBuf, int byte_to_read);
int fts_write_read(u8 *cmd, int cmd_length, u8 *out_buf, int byte_to_read);
int fts_write(u8 *cmd, int cmd_length);
int fts_write_u8ux(u8 cmd, addr_size_t addr_size, u64 address, u8 *data, int
	data_size);
int fts_write_read_u8ux(u8 cmd, addr_size_t addr_size, u64 address,
	u8 *out_buf, int byte_to_read, int has_dummy_byte);
int u8_to_u16(u8 *src, u16 *dst);
int u8_to_u16_be(u8 *src, u16 *dst);
int u8_to_u16n(u8 *src, int src_length, u16 *dst);
int u16_to_u8(u16 src, u8 *dst);
int u16_to_u8_be(u16 src, u8 *dst);
int u16_to_u8n_be(u16 *src, int src_length, u8 *dst);
int u8_to_u32(u8 *src, u32 *dst);
int u8_to_u32_be(u8 *src, u32 *dst);
int u32_to_u8(u32 src, u8 *dst);
int u32_to_u8_be(u32 src, u8 *dst);
int u8_to_u64_be(u8 *src, u64 *dest, int size);
int u64_to_u8_be(u64 src, u8 *dest, int size);
int from_id_to_mask(u8 id, u8 *mask, int size);
int fts_system_reset(int poll_event);
int fts_hdm_write_request(u8 save_to_flash);
int fts_request_hdm(u8 type);
int fts_fw_request(u16 address, u8 bit_to_set, u8 auto_clear,
	int time_to_wait);
char *print_hex(char *label, u8 *buff, int count, u8 *result);
int poll_for_event(int *event_to_search, int event_bytes,
	u8 *read_data, int time_to_wait);
int fts_write_fw_reg(u16 address, u8 *data, uint32_t length);
int fts_read_fw_reg(u16 address, u8 *read_data, uint32_t read_length);
int fts_write_hdm(u16 address, u8 *data, int length);
int fts_read_hdm(u16 address, u8 *read_data, uint32_t read_length);
int fts_read_sys_errors(void);
int read_hdm_header(uint8_t type, u8 *header);
int get_frame_data(u16 address, int size, short *frame);
int get_ms_frame(ms_frame_type_t type, struct mutual_sense_frame *frame);
int get_ss_frame(ss_frame_type_t type, struct self_sense_frame *frame);
int get_sync_frame(u8 type, struct mutual_sense_frame *ms_frame,
	struct self_sense_frame *ss_frame);
int get_mutual_cx_data(u8 type, struct mutual_sense_cx_data *ms_cx_data);
int get_self_cx_data(u8 type, struct self_sense_cx_data *ss_cx_data);
int get_mutual_total_cx_data(u8 type,
				 struct mutual_total_cx_data *tot_ms_cx_data);
int get_self_total_cx_data(u8 type, struct self_total_cx_data *tot_ss_cx_data);
int poll_fw_reg_clear_status(u16 address, u8 bit_to_check, int time_to_wait);


#endif
