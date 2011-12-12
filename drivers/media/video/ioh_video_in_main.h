/*
 * Copyright (C) 2010 OKI SEMICONDUCTOR CO., LTD.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __IOH_VIDEO_IN_H__
#define __IOH_VIDEO_IN_H__

/*! @defgroup	VideoIn */

/*! @defgroup	Global
	@ingroup	VideoIn */
/*! @defgroup	PCILayer
	@ingroup	VideoIn */
/*! @defgroup	InterfaceLayer
	@ingroup	VideoIn */
/*! @defgroup	HALLayer
	@ingroup	VideoIn */
/*! @defgroup	Utilities
	@ingroup	VideoIn */

/*! @defgroup	PCILayerAPI
	@ingroup	PCILayer */
/*! @defgroup	PCILayerFacilitators
	@ingroup	PCILayer */

/*! @defgroup	InterfaceLayerAPI
	@ingroup	InterfaceLayer */
/*! @defgroup	InterfaceLayerFacilitators
	@ingroup	InterfaceLayer */

/*! @defgroup	HALLayerAPI
	@ingroup	HALLayer */

/*! @defgroup	UtilitiesAPI
	@ingroup	Utilities */

/*! @defgroup	UtilitiesFacilitators
	@ingroup	Utilities */

/* includes */
#include <linux/ioctl.h>

/* enumerators */
/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_input_data_format
	@brief		Defines constants to denote the different supported
	input format.
	@remarks	This enum defines unique constants to denote the
			different input formats supported by the
			BT656(VideoIn) device. These constants
			can be used by the user to specify the input video
			data format to
			the driver while specifying the input settings.

	@note		The constants holds meaningful when used in
			combination with other data
			for setting input format.

	@see
		- ioh_video_in_set_input_format
		- ioh_video_in_set_output_format
		- ioh_video_in_input_format
  */
enum ioh_video_in_input_data_format {
	/* Input format for Square Pixel frequency */
	NT_SQPX_ITU_R_BT_656_4_8BIT,	/**< NTSC Square Pixel
					ITU-BT656-4 8Bit format. */
	NT_SQPX_ITU_R_BT_656_4_10BIT,	/**< NTSC Square Pixel
					ITU-BT656-4 10Bit format. */
	NT_SQPX_YCBCR_422_8BIT,		/**< NTSC Square Pixel
					YCbCr 4:2:2 8Bit format. */
	NT_SQPX_YCBCR_422_10BIT,	/**< NTSC Square Pixel
					YCbCr 4:2:2 10Bit format. */

	/* Input format for ITU-R BT.601 */
	NT_BT601_ITU_R_BT_656_4_8BIT,	/**< NTSC ITU-R BT.601
					ITU-BT656-4 8Bit format. */
	NT_BT601_ITU_R_BT_656_4_10BIT,	/**< NTSC ITU-R BT.601
					ITU-BT656-4 10Bit format. */
	NT_BT601_YCBCR_422_8BIT,	/**< NTSC ITU-R BT.601
					YCbCr 4:2:2 8Bit format. */
	NT_BT601_YCBCR_422_10BIT,	/**< NTSC ITU-R BT.601
					YCbCr 4:2:2 10Bit format. */

	/* Input format for RAW. */
	NT_RAW_8BIT,			/**< NTSC RAW 8Bit format. */
	NT_RAW_10BIT,			/**< NTSC RAW 10Bit format. */
	NT_RAW_12BIT,			/**< NTSC RAW 12Bit format. */

	/* Invalid Input Format. */
	INVALID_INPUT_DATA_FORMAT	/**< Invalid Input data format. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_numerical_format
	@brief		Defines constants indicating the different supported
			input/output numerical format.
	@remarks	This enum defines unique constants to denote the
			different numerical
			format of the video data supported by the
			BT656(VideoIn) device. These
			constants can be used by the user to specify the
			numerical format of
			the video while specifying the input and output video
			settings.
	@note		These constants holds meaningful when used along with
	other data.

	@see
		- ioh_video_in_set_input_format
		- ioh_video_in_set_output_format
		- ioh_video_in_input_format
		- ioh_video_in_output_format
  */
enum ioh_video_in_numerical_format {
	OFFSET_BINARY_FORMAT,		/**< Offset binary format. */
	COMPLEMENTARY_FORMAT_OF_2,	/**< Complementary format of 2. */
	DONT_CARE_NUMERICAL_FORMAT,	/**< Dont care. */
	INVALID_NUMERICAL_FORMAT	/**< Invalid numerical format. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_output_data_format
	@brief		Defines constants indicating the different supported
			output formats.
	@remarks	This enum defines unique constants to denote the
			different output video
			data formats supported by the BT656(VideoIn) device.
			These constants can
			be used by the user to specify the output data format
			while specifying the
			output video settings.
	@note		The constants holds meaningful when used with other
			data for setting output format.

	@see
		- ioh_video_in_set_output_format
		- ioh_video_in_output_format
  */
enum ioh_video_in_output_data_format {
	YCBCR_422_8BIT,			/**< YCbCr 4:2:2 8bits	format.	*/
	YCBCR_422_10BIT,		/**< YCbCr 4:2:2 10bits format.	*/
	YCBCR_444_8BIT,			/**< YCbCr 4:4:4 8bits format.	*/
	YCBCR_444_10BIT,		/**< YCbCr 4:4:4 10bits
	fromat. */
	RGB888,				/**< RGB888 format.	*/
	RGB666,				/**< RGB666 format.	*/
	RGB565,				/**< RGB565 format.	*/
	RAW_8BIT,			/**< RAW 8bits format.	*/
	RAW_10BIT,			/**< RAW 10bits format.	*/
	RAW_12BIT,			/**< RAW 12bits format.	*/
	INVALID_OUTPUT_DATA_FORMAT	/**< Invalid output format.	*/
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_luminance_range
	@brief		Defines constants denoting the different supported
			Luminance range.
	@remarks	This enum defines unique constants denoting the
			luminance range
			format format of the BT656(VideoIN) device. These
			constants can
			be used by the user to denote the luminance range
			format while specifying the output format settings.

	@note		The constants holds meaningful when used with other
			data for setting output format.

	@see
		- ioh_video_in_set_output_format
		- ioh_video_in_output_format
  */
enum ioh_video_in_luminance_range {
	BT601_LUMINANCE_RANGE = 0x00000000,		/**< ITU BT.601
							luminance range. */
	EXTENDENDED_LUMINANCE_RANGE = 0x00000010,	/**< Extended
							luminance range. */
	DONT_CARE_LUMINANNCE_RANGE = 0x00000011,	/**< Dont care
							luminance range. */
	INVALID_LUMINANCE_RANGE = 0x000000FF		/**< Invalid Luminance
							range. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_rgb_gain_RGBLEV
	@brief		Defines constants denoting the different supported RGB
			Level.
	@remarks	This enum defines unique constants denoting the RGB
			level format
			supported by the BT656(VideoIn device). These
			constants can be used
			by the user to denote the RGB Level setting while
			specifying the output video settings.
	@note		The constants holds meaningful when used with other
			data for setting output format.

	@see
		- ioh_video_in_set_output_format
		- ioh_video_in_output_format
  */
enum ioh_video_in_rgb_gain_RGBLEV {
	RGB_FULL_SCALE_MODE = 0x00000000,		/**< Full scale mode
							(0 - 1023). */
	RGB_BT601_MODE = 0x00000008,			/**< ITU BT.601 mode
							(64 - 940). */
	DONT_CARE_RGBLEV = 0x00000009,			/**< Dont care RGB
							Gain level. */
	INVALID_RGB_GAIN_LEVEL = 0x000000FF		/**< Invalid RGB gain
							level. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_scan_mode_method
	@brief		Defines constants denoting the different supported
			scan mode conversion methods.
	@remarks	This enum defines unique constants to denote the
			different
			interpolation methods supported by the BT656(VideoIn)
			device.
			These constants can be used by the user to specify the
			scan mode conversion methods i.e. the format for
			converting the Interlace
			input data format to Progrssive output data format.

	@see
		- ioh_video_in_set_ip_trans_mode
		- ioh_video_in_get_ip_trans_mode
  */
enum ioh_video_in_scan_mode_method {
	LINE_INTERPOLATION = 0x00000000,	/**< Line
						Interpolation Method. */
	LINE_DOUBLER = 0x00000040,		/**< Line doubler method.
						*/
	INVALID_SCAN_MODE_METHOD = 0x000000FF	/**< Invalid scan mode
						method.	*/
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_luminance_NOSIG
	@brief		Defines constants denoting the different supported
			Luminance NOSIG setting mode.
	@remarks	This enum defines unique constants to denote the NOSIG
			format supported by the BT656(VideoIn) device. These
			constants can be used by the user to denote the NOSIG
			settings while
			specifying the luminance settings.
	@note		The constants holds meaningful when used with other
	data
			for setting luminance level.

	@see
		- ioh_video_in_set_luminance_level
		- ioh_video_in_luminance_settings
  */
enum ioh_video_in_luminance_NOSIG {
	NOSIG_NORMAL_MODE = 0x00000000,		/**< Noramal mode. */
	NOSIG_NOINMASTER_MODE = 0x00000080,	/**< Image non input
						master mode. */
	INVALID_LUMINANCE_NOSIG = 0x000000FF	/**< Invalid
						luminance NOSIG mode. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_luminance_LUMLEV
	@brief		Defines constants denoting the different Luminance
			level setting mode.
	@remarks	This enum defines unique constants to denote the
			LUMLEV format supported by the BT656(VideoIn) device.
			These constants can be used by the user to denote the
			LUMLEV format while specifying the luminance settings.
	@note		The constants holds meaningful when used with other
			data for setting the luminance level.

	@see
		- ioh_video_in_set_luminance_level
		- ioh_video_in_luminance_settings
  */
enum ioh_video_in_luminance_LUMLEV {
	LUMLEV_78_PERCENT = 0x00000000,			/**<  78.125% */
	LUMLEV_81_PERCENT,				/**<  81.250% */
	LUMLEV_84_PERCENT,				/**<  84.375% */
	LUMLEV_87_PERCENT,				/**<  87.500% */
	LUMLEV_90_PERCENT,				/**<  90.625% */
	LUMLEV_93_PERCENT,				/**<  93.750% */
	LUMLEV_96_PERCENT,				/**<  96.875% */
	LUMLEV_100_PERCENT,				/**< 100.000% */
	LUMLEV_103_PERCENT,				/**< 103.125% */
	LUMLEV_106_PERCENT,				/**< 106.250% */
	LUMLEV_109_PERCENT,				/**< 109.375% */
	LUMLEV_112_PERCENT,				/**< 112.500% */
	LUMLEV_115_PERCENT,				/**< 115.625% */
	LUMLEV_118_PERCENT,				/**< 118.750% */
	LUMLEV_121_PERCENT,				/**< 121.875% */
	LUMLEV_125_PERCENT,				/**< 125.000% */
	INVALID_LUMINANCE_LUMLEV			/**< Invalid. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_blank_tim_CNTCTL
	@brief		Defines constants denoting the different supported
			CNTCTL mode
			settings for Blanking Timing Signal.
	@remarks	This enum defines unique constants to denote the
	different
			Blanking Signal Timing Control settings supported by
			the BT656(VideoIn) device. These constants can be used
			by the user while specifying the Blanking Timing Signal
			settings.
	@note		The constants holds meaningful when use with other
			data for setting the Blanking Timing Signal format.

  @see
		- ioh_video_in_set_blank_tim
		- ioh_video_in_blank_tim_settings
  */
enum ioh_video_in_blank_tim_CNTCTL {
	CNTCTL_STANDARD_SIGNAL = 0x00000000,		/**< Standard
							signal. */
	CNTCTL_NON_STANDARD_SIGNAL = 0x00000080,	/**< Non standard
							signal. */
	INVALID_BLANK_TIM_CNTCTL = 0x000000FF		/**< Invalid Blank
							tim settings. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_blank_tim_BLKADJ
	@brief		Defines constants for denoting the different supported
			BLKADJ mode settings for Blanking Timing Signal.
	@remarks	This enum defines unique constants to denote the
			different Blanking Signal Timing Adjustmemt settings
			supported by the BT656(VideoIn) device.
			These constants can be used by the
			user while specifying the Blanking Timing Signal
			settings.
	@note		The constants holds meaningful when use with other
			data for setting the Blanking Timing Signal format.

	@see
		- ioh_video_in_set_blank_tim
		- ioh_video_in_blank_tim_settings
  */
enum ioh_video_in_blank_tim_BLKADJ {
	BLKADJ_MINUS_8_PIXEL = 0x00000000,	/**< -8 pixel.	*/
	BLKADJ_MINUS_7_PIXEL,			/**< -7 pixel.	*/
	BLKADJ_MINUS_6_PIXEL,			/**< -6 pixel.	*/
	BLKADJ_MINUS_5_PIXEL,			/**< -5 pixel.	*/
	BLKADJ_MINUS_4_PIXEL,			/**< -4 pixel.	*/
	BLKADJ_MINUS_3_PIXEL,			/**< -3 pixel.	*/
	BLKADJ_MINUS_2_PIXEL,			/**< -2 pixel.	*/
	BLKADJ_MINUS_1_PIXEL,			/**< -1 pixel.	*/
	BLKADJ_0_PIXEL,				/**<  0 pixel.	*/
	BLKADJ_PLUS_1_PIXEL,			/**< +1 pixel.	*/
	BLKADJ_PLUS_2_PIXEL,			/**< +2 pixel.	*/
	BLKADJ_PLUS_3_PIXEL,			/**< +3 pixel.	*/
	BLKADJ_PLUS_4_PIXEL,			/**< +4 pixel.	*/
	BLKADJ_PLUS_5_PIXEL,			/**< +5 pixel.	*/
	BLKADJ_PLUS_6_PIXEL,			/**< +6 pixel.	*/
	BLKADJ_PLUS_7_PIXEL,			/**< +7 pixel.	*/
	INVALID_BLANK_TIM_BLKADJ		/**< Invalid.	*/
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_bb_mode
	@brief		Defines constants denoting the different supported
			Blue background mode.
	@remarks	This enum defines unique constants to denote the
			Blue Background On/Off settings. These constants
			can  be used by the user to enable/disable the
			Blue background mode.
	@note		The constants holds meaningful when use with other
			data for setting the Blue Background settings.

	@see
		- ioh_video_in_set_bb
		- ioh_video_in_get_bb
  */
enum ioh_video_in_bb_mode {
	BB_OUTPUT_OFF = 0x00000000,	/**< Blue background OFF. */
	BB_OUTPUT_ON = 0x00000040,	/**< Blue background ON. */
	INVALID_BB_MODE = 0x000000FF	/**< Invalid BB mode. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_cb_mode
	@brief		Defines constants denoting the different supported
			Color Bar mode.
	@remarks	This enum defines unique constants to denote the Color
			bar On/Off settings. These constants can be used by the
			user to enable/disable the Color Bar settings.
	@note		The constants holds meaningful when used with other
			data for Color Bar settings.

	@see
		- ioh_video_in_set_cb
		- ioh_video_in_cb_settings
  */
enum ioh_video_in_cb_mode {
	CB_OUTPUT_OFF = 0x00000000,	/**< Color Bar Mode OFF. */
	CB_OUTPUT_ON = 0x00000080,	/**< Color Bar Mode ON. */
	INVALID_CB_MODE = 0x000000FF	/**< Invalid CB mode. */
};

/*! @ingroup	InterfaceLayer
	@enum		ioh_video_in_cb_OUTLEV
	@brief		Defines constants denoting the different supported
			output level of the Color Bar.
	@remarks	This enum defines unique constants to denote the
			Output Level format of the Color Bar settings
			supported by the BT656(VideoIn) device.
			These constants can be used by the user while
			specifying the Color Bar settings.
	@note		The constants holds menaingful when used with other
			data for Color Bar settings.

	@see
		- ioh_video_in_set_cb
		- ioh_video_in_cb_settings
  */
enum ioh_video_in_cb_OUTLEV {
	CB_OUTLEV_25_PERCENT = 0x00000000,	/**<  25% Color bar.	*/
	CB_OUTLEV_50_PERCENT,			/**<  50% Color bar.	*/
	CB_OUTLEV_75_PERCENT,			/**<  75% Color bar.	*/
	CB_OUTLEV_100_PERCENT,			/**< 100% Color bar.	*/
	INVALID_CB_OUTLEV			/**< Invalid.		*/
};

/* structures */
/*! @ingroup	InterfaceLayer
	@struct		ioh_video_in_input_format
	@brief		The structure used to specify settings of a particular
			input format.
	@remarks	This structure defines the fields used to set/get the
			input format settings of the BT656(VideoIn) device. The
			user has to fill the individual fields with the unique
			constants denoting the respective settings and pass on
			to the driver through the respective ioctl calls.
	@note		The fields specify enum constants which are used to
			specify the input format.

  @see
		- ioh_video_in_set_input_format
		- ioh_video_in_get_input_format
  */
struct ioh_video_in_input_format {
	/*Input format */
	enum ioh_video_in_input_data_format format;	/**< The input
							video data format. */

	/*IN2S Settings */
	enum ioh_video_in_numerical_format numerical_format;
						/**< The input
						video numerical format.	*/
};

/*! @ingroup	InterfaceLayer
	@struct		ioh_video_in_output_format
	@brief		Structures used to specify the settings of a
			particular output format.
	@remarks	This structure defines the fileds used to set/get the
			output format settings of the BT656(VideoIn) device.
			The user has to fill the individual fields with the
			unique constants denoting the respective settings and
			pass on to the driver through the respective ioctl
			calls.
	@note		The fields are constants denoting specific information
			about the output format.

	@see
		- ioh_video_in_set_output_format
		- ioh_video_in_get_output_format
  */
struct ioh_video_in_output_format {
	/*Output data format */
	enum ioh_video_in_output_data_format format;
				/**< The output video data format. */

	/*OUT2S Settings */
	enum ioh_video_in_numerical_format numerical_format;
				/**< The output video numerical format. */

	/*SBON Settings */
	enum ioh_video_in_luminance_range luminance_range;
				/**< The luminance range format. */

	/*RGBLEV Settings */
	enum ioh_video_in_rgb_gain_RGBLEV rgb_gain_level;
				/**< The RGB gain level format. */
};

/*! @ingroup	InterfaceLayer
	@struct		ioh_video_in_luminance_settings
	@brief		Structure used to specify the settings for Luminance
			level.
	@remarks	This structure defines the fields used to set/get the
			luminanace settings of the BT656(VideoIn) device.
			The user has to fill the individual fields with the
			unique constants denoting the respective
			settings and pass on to the driver through the
			respective ioctl calls.
	@note		The fields are enum constants denoting the different
			settings for luminance level.

	@see
		- ioh_video_in_set_luminance_level
		- ioh_video_in_get_luminance_level
  */
struct ioh_video_in_luminance_settings {
	enum ioh_video_in_luminance_NOSIG luminance_nosig;
				/**< The NOSIG settings. */
	enum ioh_video_in_luminance_LUMLEV luminance_lumlev;
				/**< The LUMLEV settings. */
};

/*! @ingroup	InterfaceLayer
	@struct		ioh_video_in_rgb_gain_settings
	@brief		Structure used to specify the RGB Gain level.
	@remarks	This structure defines the fields used to set/get the
			RGB gain settings of the BT656(VideoIn) device.
			The fields denotes the 8bit register values that has
			to be filled in by the user and pass on to the driver
			through the respective ioctl call for setting the RGB
			gain settings.
	@see
		- ioh_video_in_set_rgb_gain
		- ioh_video_in_get_rgb_gain
  */
struct ioh_video_in_rgb_gain_settings {
	unsigned char r_gain;			/**< R gain (Values should be
						between 0-255).	*/
	unsigned char g_gain;			/**< G gain (Values should be
						between 0-255).	*/
	unsigned char b_gain;			/**< B gain (Values should be
						between 0-255).	*/
};

/*! @ingroup	InterfaceLayer
	@struct		ioh_video_in_blank_tim_settings
	@brief		Structure used to specify the Blanking Timing Signal
			Settings.
	@remarks	This structure defines the fields used to set/get the
			Blanking Timing signal settings of the BT656(VideoIn)
			device.
			These fields have to be set by the user with unique
			constants denoting the respective settings and pass
			on to the driver through the respective ioctl calls.
	@note		The fields are enum constants denoting the different
			settings of the Blanking Timing Signal.

	@see
		- ioh_video_in_set_blank_tim
		- ioh_video_in_get_blank_tim
  */
struct ioh_video_in_blank_tim_settings {
	enum ioh_video_in_blank_tim_CNTCTL blank_tim_cntctl;
			/**< Blanking Timing Signal Control settings. */
	enum ioh_video_in_blank_tim_BLKADJ blank_tim_blkadj;
			/**< Blanking Timing Signal Adjustment settings.*/
};

/*! @ingroup	InterfaceLayer
	@struct		ioh_video_in_cb_settings
	@breif		Structure used to specify the Color bar settings.
	@remarks	This structure defines the fields used to set/get the
			Color Bar settings of the BT656(VideoIn) device. These
			fields have to be set by the user with unique constants
			denoting the respective settings and pass on to the
			driver through the respective ioctl calls.
	@note		The fields are enum constants denoting the different
			Color Bar formats.

	@see
		- ioh_video_in_set_cb
		- ioh_video_in_get_cb
  */
struct ioh_video_in_cb_settings {
	enum ioh_video_in_cb_mode cb_mode;
				/**< Color Bar ON/OFF mode. */
	enum ioh_video_in_cb_OUTLEV cb_outlev;
				/**< Color Bar Otput level settings. */
};

/*! @ingroup	InterfaceLayer
	@struct		ioh_video_in_frame_size
	@breif		Structure used to specify the framew size settings.
	@remarks	This structure defines the fields used to set/get the
			frame size settings of the BT656(VideoIn) device.
			These fields have to be set by the user with
			X and Y components of the frmae and pass on to the
			driver through the respective ioctl calls.
	@note		The fields denote the X and Y components of the frame.

	@see
		- ioh_video_in_set_cb
		- ioh_video_in_get_cb
  */
struct ioh_video_in_frame_size {
	unsigned int X_component;	/**< The X_component of the frame. */
	unsigned int Y_component;	/**< The Y_component of the frame. */
	unsigned int pitch_size;	/**< Pitch byte size */
};

/*! @ingroup	InterfaceLayer
	@struct		ioh_video_in_frame_buffer
	@brief		The structure for holding the video frame data.
*/
struct ioh_video_in_frame_buffer {
	int index;		/* Buffer index */
	unsigned int virt_addr;	/* Frame Buffer virtaul address */
	unsigned int phy_addr;	/* Frame Buffer Physical address */
	unsigned int data_size;	/* data size */
};

/*! @ingroup	VideoIn
	@def		MAXIMUM_FRAME_BUFFERS
	@brief		Maximum frame buffers to be allocated.
  */
#define MAXIMUM_FRAME_BUFFERS		(5)

/*! @ingroup	VideoIn
	@struct		ioh_video_in_frame_buffer_info
	@brief		The structure for holding the video frame information.
*/
struct ioh_video_in_frame_buffer_info {
	int buffer_num;		/* Number of frame buffer */
	int order;		/* Page number log2 N of the frame buffer */
};

/*! @ingroup	VideoIn
	@struct		ioh_video_in_frame_buffers
	@brief		The structure of some frame buffers.
*/
struct ioh_video_in_frame_buffers {
	struct ioh_video_in_frame_buffer frame_buffer[MAXIMUM_FRAME_BUFFERS];
};

/*! @ingroup	InterfaceLayer
	@def		VIDEO_IN_IOCTL_MAGIC
	@brief		Outlines the byte value used to define the differnt
			ioctl commands.
*/
#define VIDEO_IN_IOCTL_MAGIC	'V'
#define BASE			BASE_VIDIOC_PRIVATE

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_INPUT_FORMAT
	@brief		Outlines the value specifing the ioctl command for
			setting input format.
	@remarks	This ioctl command is issued to set the input format
			settings. The parameter expected for this is a user
			level address which points to a variable of type
			struct ioh_video_in_input_format and it contains
			values specifying the input format to be set.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_INPUT_FORMAT	(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 1, struct ioh_video_in_input_format))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_INPUT_FORMAT
	@brief		Outlines the value specifing the ioctl command for
			getting the current input format
	@remarks	This ioctl command is issued for getting the current
			input format settings.
			The expected parameter for this command is a user
			level address which points to a variable of type struct
			ioh_video_in_input_format, to which the current input
			setting has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_INPUT_FORMAT	(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 2, struct ioh_video_in_input_format))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_OUTPUT_FORMAT
	@brief		Outlines the value specifing the ioctl command for
			setting output format.
	@remarks	This ioctl command is issued to set the output format
			settings. The expected parameter is a user level
			address which points to a variable of type
			struct ioh_video_in_output_format and it contains
			values specifying the required output format.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_OUTPUT_FORMAT	(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 3, struct ioh_video_in_output_format))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_OUTPUT_FORMAT
	@brief		Outlines the value specifing the ioctl command for
			getting the current output format.
	@remarks	This ioctl command is issued for getting the current
			output format settings.
			The expected parameter is a user level address
			pointing to a variable of type
			struct ioh_video_in_output_format, to which the
			current output setting has to
			be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_OUTPUT_FORMAT	(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 4, struct ioh_video_in_output_format))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_SIZE
	@brief		Outlines the value specifing the ioctl command for
			setting the frame size.
	@remarks	This ioctl command is issued for setting the frame
			size. The expected parameter
			is a user level address pointing to a variable of type
			struct ioh_video_in_frame_size
			and it contains the frame size value that has to be
			set.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_SIZE		(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 5, struct ioh_video_in_frame_size))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_SIZE
	@brief		Outlines the value specifing the ioctl command for
			getting the current frame size.
	@remarks	This ioctl command is issued for getting the current
			frame size. The expected
			parameter is a user level address pointing to a
			variable of type struct	ioh_video_in_frame_size
			to which the current frame size has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_SIZE		(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 6, struct ioh_video_in_frame_size))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_IP_TRANS
	@brief		Outlines the value specifing the ioctl command for
			setting the scan mode conversion method.
	@remarks	This ioctl command is issued for setting the scan mode
			conversion method. The expected
			parameter is a user level address that points to a
			variable of type enum ioh_video_in_scan_mode_method,
			and it contains a value specifying the scan mode
			conversion method that has to be set.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_IP_TRANS		(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 7, enum ioh_video_in_scan_mode_method))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_IP_TRANS
	@brief		Outlines the value specifing the ioctl command for
			getting the current scan mode conversion method.
	@remarks	This ioctl command is issued for getting the current
			scan mode conversion method. The expected
			parameter is a user level address that points to a
			variable of type enum
			ioh_video_in_scan_mode_method to which the current
			scan mode conversion method setting
			has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_IP_TRANS		(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 8, enum ioh_video_in_scan_mode_method))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_LUMINENCE_LEVEL
	@brief		Outlines the value specifing the ioctl command for
			setting the luminance level settings.
	@remarks	This ioctl command is issued for setting the luminance
			level of the output video. The expected
			parameter is a user level address that points to a
			variable of type struct
			ioh_video_in_luminance_settings, and it contains
			values specifying the required luminance settings.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_LUMINENCE_LEVEL	(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 9, struct ioh_video_in_luminance_settings))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_LUMINENCE_LEVEL
	@brief		Outlines the value specifing the ioctl command for
			getting the current luminance level settings.
	@remarks	This ioctl command is issued for getting the current
			luminance settings. The expected parameter is a user
			level address pointing to a variable of type
			struct ioh_video_in_luminance_settings,
			to which the settings has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_LUMINENCE_LEVEL	(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 10, struct ioh_video_in_luminance_settings))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_RGB_GAIN
	@brief		Outlines the value specifing the ioctl command for
			setting the RGB gain level.
	@remarks	This ioctl command is issued for setting the RGB gain
			settings. The expected parameter
			is a user level address which points to a variable of
			type struct ioh_video_in_rgb_gain_settings
			and it contains values specifying the required RGB Gain
			settings.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_RGB_GAIN		(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 11, struct ioh_video_in_rgb_gain_settings))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_RGB_GAIN
	@brief		Outlines the value specifing the ioctl command for
			getting the current luminance level setting.
	@remarks	This ioctl command is issued for getting the current
			RGB Gain settings. The expected
			parameter is a user level address that points to a
			variable of type struct ioh_video_in_rgb_gain_settings,
			to which the settings has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_RGB_GAIN		(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 12, struct ioh_video_in_rgb_gain_settings))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_CAP_START
	@brief		Outlines the value specifing the ioctl command for
			initiating the video capture process.
	@remarks	This ioctl command is issued to start capturing the
			video data. This command does not
			expect any parameter.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_CAP_START		(_IO(VIDEO_IN_IOCTL_MAGIC,\
BASE + 13))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_CAP_STOP
	@brief		Outlines the value specifing the ioctl command for
			stopping the video capturing process.
	@remarks	This ioctl command is issued to stop capturing the
			video data. This command does not expect any parameter.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_CAP_STOP		(_IO(VIDEO_IN_IOCTL_MAGIC,\
BASE + 14))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_BLANK_TIM
	@brief		Outlines the value specifing the ioctl command for
			setting Blanking Timing Signal.
	@remarks	This ioctl command is issued for setting the Blanking
			Signal timing. The expected parameter is a user level
			address which points to a variable of type
			struct ioh_video_in_blank_tim_settings and it contains
			the values specifying the required settings.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_BLANK_TIM		(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 15, struct ioh_video_in_blank_tim_settings))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_BLANK_TIM
	@brief		Outlines the value specifing the ioctl command for
			getting the current Blanking Timing Signal.
	@remarks	This ioctl command is issued for getting the current
			Blanking Signal timing settings.
			The expected parameter is a user level address which
			points to a variable of type
			struct ioh_video_in_blank_tim_settings, to which the
			current settings has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_BLANK_TIM		(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 16, struct ioh_video_in_blank_tim_settings))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_BB
	@brief		Outlines the value specifing the ioctl command for
			setting Blue background mode.
	@remarks	This ioctl command is issued for setting the Blue
			Background settings. The expected
			parameter is a user level address which points to
			variable of type enum
			ioh_video_in_bb_mode
			and it contains the value specifying the required
			settings.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_BB		(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 17, enum ioh_video_in_bb_mode))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_BB
	@brief		Outlines the value specifing the ioctl command for
			getting the current Blue background mode.
	@remarks	This ioctl command is issued for getting the current
			Blue background settings. The
			expected parameter is a user level address
			which points to a variable of type
			enum ioh_video_in_bb_mode,
			to which the current settings has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_BB		(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 18, enum ioh_video_in_bb_mode))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_SET_CB
	@brief		Outlines the value specifing the ioctl command for
			setting Color bar output level.
	@remarks	This ioctl command is issued for setting the Color Bar
			settings. The expected parameter is a
			user level address which points to a variable of type
			struct ioh_video_in_cb_settings and it
			contains values specifying the required settings.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_SET_CB		(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 19, struct ioh_video_in_cb_settings))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_CB
	@brief		Outlines the value specifing the ioctl command for
			getting the current Color bar output level.
	@remarks	This ioctl command is issued for getting the current
			color bar settings. The expected
			parameter is a user level address which points to a
			variable of type struct
			ioh_video_in_cb_settings, to which the current
			settings has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_CB		(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 20, struct ioh_video_in_cb_settings))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_BUFFER_SIZE
	@brief		Outlines the value specifing the ioctl command for
			getting the buffer size.
	@remarks	This ioctl command is issued for getting the buffer
			size. The expected parameter is a
			user level address which points to a variable of type
			unsigned long, to which the buffer
			size has to be updated.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_BUFFER_SIZE	(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 21, unsigned long))

#if 0
/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_ALLOC_FRAME_BUFFER
	@brief		Outlines the value specifing the ioctl command for
			allocate the frame buffers.
	@remarks	This ioctl command is issued to allocate the
			frame buffers.
			The expected parameter is a user level address which
			points to a variable of type struct
			ioh_video_in_frame_buffer_info.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_ALLOC_FRAME_BUFFER	(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 22, struct ioh_video_in_frame_buffer_info))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_FREE_FRAME_BUFFER
	@brief		Outlines the value specifing the ioctl command for
	free the frame buffers.
	@remarks	This ioctl command is issued to free the
			frame buffers.
			The expected parameter is a user level address which
			points to a variable of type struct
			ioh_video_in_frame_buffer_info.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_FREE_FRAME_BUFFER	(_IOW(VIDEO_IN_IOCTL_MAGIC,\
BASE + 23, struct ioh_video_in_frame_buffer_info))
#endif

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_GET_FRAME_BUFFERS
	@brief		Outlines the value specifing the ioctl command for
			read the information of the frame buffers.
	@remarks	This ioctl command is issued to get the information of
			frame buffers.
			The expected parameter is a user level address which
			points to a variable of type struct
			ioh_video_in_frame_buffer.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_GET_FRAME_BUFFERS	(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 24, struct ioh_video_in_frame_buffers))

/*! @ingroup	InterfaceLayer
	@def		IOH_VIDEO_READ_FRAME_BUFFER
	@brief		Outlines the value specifing the ioctl command for
			read the frame buffer.
	@remarks	This ioctl command is issued to get frame buffer.
			The expected parameter is a user level address which
			points to a variable of type struct
			ioh_video_in_frame_buffer.
	@see
		- ioh_video_in_ioctl
  */
#define IOH_VIDEO_READ_FRAME_BUFFER	(_IOR(VIDEO_IN_IOCTL_MAGIC,\
BASE + 25, struct ioh_video_in_frame_buffer))


/*! @ingroup	VideoIn
	@def		IOH_VIDEOIN_SUCCESS
	@brief		Outlines the return value of the function on success.
*/
#define IOH_VIDEOIN_SUCCESS (0)

/*! @ingroup	VideoIn
	@def		IOH_VIDEOIN_FAIL
	@brief		Outlines the return value of the function on failure.
  */
#define IOH_VIDEOIN_FAIL	(-1)


#endif


