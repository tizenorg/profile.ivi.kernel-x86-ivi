/* -*- pse-c -*-
 *-----------------------------------------------------------------------------
 * Filename: splash_screen.c
 * $Revision: 1.5 $
 *-----------------------------------------------------------------------------
 * Copyright (c) 2002-2010, Intel Corporation.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *-----------------------------------------------------------------------------
 * Description:
 *  This is the Intel Embedded Graphics EFI Driver Splash Screen implementation
 *  file. This code shows a splash screen with a customizable icon.
 *-----------------------------------------------------------------------------
 */
#define MODULE_NAME hal.oal

#include <drm/drmP.h>
#include <drm/drm.h>
#include "image_data.h"
#include "splash_screen.h"
#include "io.h"
#include "igd_debug.h"


/**
 * Function to display a splash screen to the user. The splash screen must be
 * accessible to the kernel mode driver as it has to be displayed immediately
 * after setting the mode (if requested by the user through config options).
 *
 * @param ss_data (IN) a non null pointer to splash screen information like
 * width, height etc.
 */
void display_splash_screen(
	igd_framebuffer_info_t *fb_info,
	unsigned char *fb,
	emgd_drm_splash_screen_t *ss_data)
{
	if (image_data[0] == 0x89) {
		display_png_splash_screen(fb_info, fb, ss_data);
	} else {
		display_bmp_splash_screen(fb_info, fb, ss_data);
	}
}

/*
 * This is the function to display the bmp splash screen.
 *
 * @param ss_data (IN) a non null pointer to splash screen information like
 * width, height etc.
 */
void display_bmp_splash_screen(
	igd_framebuffer_info_t *fb_info,
	unsigned char *fb,
	emgd_drm_splash_screen_t *ss_data)
{
	unsigned char *fb_addr, *icon_temp;
	unsigned long *fb_addr_long, icon_long;
	unsigned long bitmap_pitch;
	short x, y;
	unsigned long init_x_shift, init_y_shift;
	unsigned long row, col, fb_index;
	unsigned long bytecount, temp;

	x = (short) ss_data->x;
	y = (short) ss_data->y;

	if(x < 0) {
		init_x_shift = (fb_info->width + x) * 4;
		init_y_shift = (fb_info->height + y) * fb_info->screen_pitch;
	} else {
		init_x_shift = x * 4;
		init_y_shift = y * fb_info->screen_pitch;
	}

	fb_addr = fb + init_y_shift;
	bytecount = (unsigned long) image_data[1];
	bitmap_pitch = ss_data->width * bytecount;

	for(row = 0; row < ss_data->height; row++) {
		fb_addr_long =
			(unsigned long *) &fb_addr[fb_info->screen_pitch * row +
			init_x_shift];
		/*
		 * We are adding 3 bytes here, the first byte indicates BMP or PNG,
		 * the second byte is the bytecount
		 * and the third byte is the palette count
		 */
		icon_temp = &image_data[3 + (row * bitmap_pitch)];
		fb_index = 0;

		for(col = 0; col < ss_data->width; col++) {

			icon_long = *((unsigned long *) &icon_temp[col*bytecount]);
			switch(bytecount) {
				case 1:
					/* 8 bit */
					temp = (icon_long & 0xFF);
					icon_long = ((temp & 0xE0)<<16) | ((temp & 0x1C)<<11) |
						((temp & 0x3)<<6);
					break;
				case 2:
					/* 16 bit */
					temp = (icon_long & 0xFFFF);
					icon_long = CONV_16_TO_32_BIT(temp);
					break;
			}
			/*
			 * For 24 bit we don't really have to do anything as it is
			 * already in RGB 888 format
			 */
			fb_addr_long[fb_index++] = icon_long & 0x00FFFFFF;
		}
	}
}


/*
 * This is the function to display the png splash screen.
 *
 * @param ss_data (IN) a non null pointer to splash screen information like
 * width, height etc.
 */
void display_png_splash_screen(
	igd_framebuffer_info_t *fb_info,
	unsigned char *fb,
	emgd_drm_splash_screen_t *ss_data)
{
	unsigned long image_size;
	unsigned long i,j,k;
	unsigned long chunk_size;
	unsigned long chunk_type;
	unsigned int bpp = 32;
	unsigned int bytes_pp = 4;
	unsigned long bytes_pl = 4;
	unsigned long iter = PNG_HEADER_SIZE;
	unsigned char bit_iter = 0;
	png_header image_header;
	unsigned long *image_palette = 0;
	unsigned long gama = 0;
	unsigned long palette_size = 0;
	unsigned long filter_type = 0;
	unsigned char zlib_cmf = 0;
	unsigned char zlib_flg = 0;
	unsigned char zlib_cm = 0;
	unsigned char zlib_cinfo = 0;
	unsigned char zlib_fcheck = 0;
	unsigned char zlib_fdict = 0;
	unsigned char zlib_flevel = 0;
	unsigned long zlib_dictid = 0;
	unsigned long bfinal = 0;
	unsigned long btype = 0;
	unsigned char compr_len = 0;
	unsigned char compr_nlen = 0;
	huffman_node *length_tree = 0;
	huffman_node *distance_tree = 0;

	unsigned char *output = 0;
	unsigned char *input_data = 0;
	unsigned long input_size = 0;
	unsigned long input_iter = 0;
	unsigned long output_iter = 0;

	unsigned char *fb_addr;
	unsigned long *fb_addr_long;
	short x, y;
	unsigned long row = 0, col = 0;
	unsigned long init_x_shift, init_y_shift;
	unsigned char image_alpha;
	unsigned char background_alpha;
	unsigned long background;
	unsigned char background_r;
	unsigned char background_g;
	unsigned char background_b;
	unsigned long end_of_row;
	unsigned int small_color;

	unsigned char paeth_a, paeth_b, paeth_c;
	unsigned long paeth_p, paeth_pa, paeth_pb, paeth_pc;

	x = (short) ss_data->x;
	y = (short) ss_data->y;

	/*
	 * Just incase there is no background and we have alpha values, lets
	 * use the background specified in ss_data.
	 */
	background = ss_data->bg_color;
	background_r = (background >> 16) & 0xFF;
	background_g = (background >> 8) & 0xFF;
	background_b = background & 0xFF;

	image_size = sizeof(image_data)/sizeof(unsigned char);
	input_data = (unsigned char *)kzalloc(sizeof(image_data), GFP_KERNEL);
    if (input_data == NULL) {
           return;
    }

	/*
	 * Lets get the information for the first chunk, which should be
	 * the header chunk: IHDR.
	 */
	read_int_from_stream(image_data, &iter, &chunk_size);
	read_int_from_stream(image_data, &iter, &chunk_type);

	/*
	 * Initialize image_header
	 */
	image_header.width = 0;
	image_header.height = 0;
	image_header.bit_depth = 0;
	image_header.colour_type = 0;
	image_header.compression_method = 0;
	image_header.filter_method = 0;
	image_header.interlace_method = 0;

	/* Loop through the PNG chunks */
	while ((chunk_type != CHUNK_IEND) && (iter <= image_size)) {
		switch (chunk_type) {
		case CHUNK_IHDR:
			read_int_from_stream(image_data, &iter, &image_header.width);
			read_int_from_stream(image_data, &iter, &image_header.height);
			image_header.bit_depth = (unsigned char)image_data[iter++];
			image_header.colour_type = (unsigned char)image_data[iter++];
			image_header.compression_method = (unsigned char)image_data[iter++];
			image_header.filter_method = (unsigned char)image_data[iter++];
			image_header.interlace_method = (unsigned char)image_data[iter++];

			/* store bits per pixel based on PNG spec */
			switch (image_header.colour_type) {
			case COLOR_GREY:
				bpp = image_header.bit_depth;
				break;
			case COLOR_TRUE:
				bpp = 3 * image_header.bit_depth;
				break;
			case COLOR_INDEXED:
				bpp = image_header.bit_depth;
				break;
			case COLOR_GREY_ALPHA:
				bpp = 2 * image_header.bit_depth;
				break;
			case COLOR_TRUE_ALPHA:
				bpp = 4 * image_header.bit_depth;
				break;
			}
			/*
			 * Adding 7 to the bits per pixel before we divide by 8
			 * gives us the ceiling of bytes per pixel instead of the floor.
			 */
			bytes_pp = (bpp + 7) / 8;
			bytes_pl = ((image_header.width * bpp) + 7) / 8;

			/* Allocate space for out output buffer */
			output = (unsigned char *)kzalloc(
				image_header.height * bytes_pl + image_header.height,
				GFP_KERNEL);
            if (output == NULL) {
                   return;
            }

			break;

		case CHUNK_BKGD:
			/* Truecolor */
			if (image_header.colour_type == COLOR_TRUE_ALPHA ||
				image_header.colour_type == COLOR_TRUE) {

				switch (image_header.bit_depth) {
				case 16:
					read_char_from_stream(image_data, &iter, &background_r);
					iter++;
					read_char_from_stream(image_data, &iter, &background_g);
					iter++;
					read_char_from_stream(image_data, &iter, &background_b);
					iter++;
					break;
				case 8:
					iter++;
					read_char_from_stream(image_data, &iter, &background_r);
					iter++;
					read_char_from_stream(image_data, &iter, &background_g);
					iter++;
					read_char_from_stream(image_data, &iter, &background_b);
					break;
				}
			}

			/* Grayscale */
			if (image_header.colour_type == COLOR_GREY_ALPHA ||
				image_header.colour_type == COLOR_GREY) {

				switch (image_header.bit_depth) {
				case 16:
					read_char_from_stream(image_data, &iter, &background_r);
					iter++;
					break;
				case 8:
					iter++;
					read_char_from_stream(image_data, &iter, &background_r);
					break;
				case 4:
					iter++;
					read_char_from_stream(image_data, &iter, &background_r);
					background_r = ((background_r >> 4) << 4) |
									(background_r >> 4);
					break;
				case 2:
					iter++;
					read_char_from_stream(image_data, &iter, &background_r);
					background_r = ((background_r >> 2) << 6) |
									((background_r >> 2) << 4) |
									((background_r >> 2) << 2) |
									(background_r >> 2);
					break;
				case 1:
					iter++;
					read_char_from_stream(image_data, &iter, &background_r);
					background_r = (background_r << 7) | (background_r << 6) |
									(background_r << 5) | (background_r << 4) |
									(background_r << 3) | (background_r << 2) |
									(background_r << 1) | background_r;
					break;
				}
				background_g = background_r;
				background_b = background_r;
			}

			background = 0x00FFFFFF &
				(background_r<<16 | background_g<<8 | background_b);
			break;

		case CHUNK_GAMA:
			read_int_from_stream(image_data, &iter, &gama);
			break;

		case CHUNK_PLTE:
			palette_size = chunk_size/3;
			image_palette = kzalloc(sizeof(unsigned long) * palette_size,
				GFP_KERNEL);
            if (image_palette == NULL) {
                   return;
            }

			for (i=0; i<palette_size; i++) {
				image_palette[i] = (
					((unsigned char)image_data[iter] << 16) |
					((unsigned char)image_data[iter+1] << 8) |
					(unsigned char)image_data[iter+2]);
				iter += 3;
			}
			break;

		case CHUNK_IDAT:
			input_size += chunk_size;
			for (i=0; i<chunk_size; i++) {
				input_data[input_iter++] = image_data[iter++];
			}
			break;

		default:
			iter += chunk_size;
			break;
		}

		/*
		 * Skip over the CRC for now, do we actually want to spend
		 * time checking this? Per the spec, unless there is an a corrupted
		 * header, the only possible outcome is a corrupted image.  It's
		 * either that, or we don't display any image, maybe in this case we
		 * should display a blue screen. :)
		 */
		iter += 4;

		/* Get the next chunk */
		read_int_from_stream(image_data, &iter, &chunk_size);
		read_int_from_stream(image_data, &iter, &chunk_type);
	}

	iter = 0;
	bit_iter = 0;

	/* Data, this needs to be decompressed per zlib spec */
	if (!zlib_cmf) {
		zlib_cmf = (unsigned char)input_data[iter++];
		zlib_flg = (unsigned char)input_data[iter++];
		zlib_cm = zlib_cmf & 0xF;
		zlib_cinfo = (zlib_cmf >> 4) & 0xF;
		zlib_fcheck = zlib_flg & 0x1F;
		zlib_fdict = (zlib_flg & 0x20) >> 5;
		zlib_flevel = (zlib_flg >> 6) & 0x3;

		if (zlib_fdict) {
			read_int_from_stream(input_data, &iter, &zlib_dictid);
		}
	}

	/* Here is where we need to process data as a bit stream */
	bfinal = 0;
	while (!bfinal) {
		read_bits_from_stream(input_data, &iter, &bit_iter, 1, &bfinal);
		read_bits_from_stream(input_data, &iter, &bit_iter, 2, &btype);

		if (btype == 0){

			if (bit_iter) {
				iter++;
				bit_iter = 0;
			}

			/* No Compression */
			read_char_from_stream(input_data, &iter, &compr_len);
			read_char_from_stream(input_data, &iter, &compr_nlen);

			for (j = 0;j < compr_len; j++) {
				read_char_from_stream(input_data, &iter, &output[j]);
			}

		} else {

			if (btype == 2){

				/* Compressed with dynamnic Huffman codes */
				build_dynamic_huffman_tree(
						input_data,
						&iter,
						&bit_iter,
						&length_tree,
						&distance_tree);
			} else {

				/* Compressed with static Huffman codes */
				build_static_huffman_tree(&length_tree,	&distance_tree);
			}

			/* Decompress huffman code */
			decompress_huffman(
					input_data,
					&iter,
					&bit_iter,
					&length_tree,
					&distance_tree,
					output,
					&output_iter);

			free_node(length_tree);
			free_node(distance_tree);
		}
	}

	/* we are now done with input_data */
	kfree(input_data);

	/* Handle the situation where the user positions the image off screen */
	if (image_header.width + x > fb_info->screen_pitch) {
		image_header.width = fb_info->screen_pitch - x;
	}
	if (image_header.height + y > fb_info->height) {
		image_header.height = fb_info->height - y;
	}

	/* Lets position our image at the supplied offsets on the screen */
	if(x < 0) {
		init_x_shift = (fb_info->width + x) * 4;
		init_y_shift = (fb_info->height + y) * fb_info->screen_pitch;
	} else {
		init_x_shift = x * 4;
		init_y_shift = y * fb_info->screen_pitch;
	}
	fb_addr = fb + init_y_shift;
	fb_addr_long = (unsigned long *) &fb_addr[init_x_shift];

	row = 0;
	j = 0;
	/*
	 * Process the scanline filtering
	 * This filtering works by using a difference from a previous pixel
	 * instead of full pixel data.
	 */
	while (row < image_header.height){
		j = row * bytes_pl + row;
		end_of_row = j + bytes_pl;
		filter_type = output[j++];

		switch (filter_type) {
		case 1:
			/* Filter type of 1 uses the previous pixel */
			for (k=j+bytes_pp; k<=end_of_row; k++) {
				output[k] += output[k-bytes_pp];
			}
			break;
		case 2:
			/* Filter type of 2 uses the previous row's pixel */
			if (row) {
				for (k=j; k<=end_of_row; k++) {
					output[k] += output[k-bytes_pl-1];
				}
			}
			break;
		case 3:
			/*
			 * Filter type of 3 uses the average of the
			 * previous pixel and the previous row's pixel
			 */
			if (row) {
				for (k=j; k<j+bytes_pp; k++) {
					output[k] += output[k-bytes_pl-1]/2;
				}
				for (k=j+bytes_pp; k<=end_of_row; k++) {
					output[k] += (output[k-bytes_pp] +
						output[k-bytes_pl-1])/2;
				}
			} else {
				for (k=j+bytes_pp; k<=end_of_row; k++) {
					output[k] = output[k] +
						output[k-bytes_pp]/2;
				}
			}
			break;
		case 4:
			/*
			 * Filter type of 4 uses this algorithm to
			 * determine if it should use the previous pixel,
			 * the previous row's pixel, or the pixel immediately
			 * before the previous row's pixel.
			 */
			for (k=j; k<=end_of_row; k++) {

				if (k >= j + bytes_pp) {
					paeth_a = output[k-bytes_pp];
				} else {
					paeth_a = 0;
				}

				if (row) {
					paeth_b = output[k-bytes_pl-1];
				} else {
					paeth_b = 0;
				}

				if (row && k >= j + bytes_pp) {
					paeth_c = output[k-bytes_pp-bytes_pl-1];
				} else {
					paeth_c = 0;
				}

				paeth_p = paeth_a + paeth_b - paeth_c;
				paeth_pa = abs(paeth_p - paeth_a);
				paeth_pb = abs(paeth_p - paeth_b);
				paeth_pc = abs(paeth_p - paeth_c);

				if (paeth_pa <= paeth_pb && paeth_pa <= paeth_pc) {
					output[k] += paeth_a;
				} else if (paeth_pb <= paeth_pc) {
					output[k] += paeth_b;
				} else {
					output[k] += paeth_c;
				}
			}
			break;
		}

		col = 0;
		fb_addr_long = (unsigned long *)
			&fb_addr[fb_info->screen_pitch * row + init_x_shift];

		/* Put together the pixel and output to framebuffer */
		while (col < image_header.width) {

			/* Truecolor with alpha, 16 bits per component */
			if (image_header.colour_type == COLOR_TRUE_ALPHA &&
				image_header.bit_depth == 16) {

				if (output[j+6] & 0xFF){
					if ((output[j+6] & 0xFF) != 0xFF){
						image_alpha = output[j+6] & 0xFF;
						background_alpha = (0xFF - image_alpha) & 0xFF;

						output[j] =
							(0xFF&((output[j] * image_alpha)/0xFF)) +
							(0xFF&((background_r * background_alpha)/0xFF));

						output[j+2] =
							(0xFF&((output[j+2] * image_alpha)/0xFF)) +
							(0xFF&((background_g * background_alpha)/0xFF));

						output[j+4] =
							(0xFF&((output[j+4] * image_alpha)/0xFF)) +
							(0xFF&((background_b * background_alpha)/0xFF));
					}

					fb_addr_long[col] = 0x00FFFFFF &
						(output[j]<<16 |
						output[j+2]<<8 |
						output[j+4]);
				}
			}


			/* Truecolor with alpha, 8 bits per component */
			if (image_header.colour_type == COLOR_TRUE_ALPHA &&
				image_header.bit_depth == 8) {

				if (output[j+3] & 0xFF){
					if ((output[j+3] & 0xFF) != 0xFF){
						image_alpha = output[j+3] & 0xFF;
						background_alpha = (0xFF - image_alpha) & 0xFF;

						output[j] =
							(0xFF&((output[j] * image_alpha)/0xFF)) +
							(0xFF&((background_r * background_alpha)/0xFF));

						output[j+1] =
							(0xFF&((output[j+1] * image_alpha)/0xFF)) +
							(0xFF&((background_g * background_alpha)/0xFF));

						output[j+2] =
							(0xFF&((output[j+2] * image_alpha)/0xFF)) +
							(0xFF&((background_b * background_alpha)/0xFF));
					}
					fb_addr_long[col] = 0x00FFFFFF &
						(output[j]<<16 |
						output[j+1]<<8 |
						output[j+2]);
				}
			}

			/* Grayscale with alpha, 16 bits per component */
			if (image_header.colour_type == COLOR_GREY_ALPHA &&
				image_header.bit_depth == 16) {

				if (output[j+2] & 0xFF){
					if ((output[j+2] & 0xFF) != 0xFF){
						image_alpha = output[j+2] & 0xFF;
						background_alpha = (0xFF - image_alpha) & 0xFF;

						output[j] =
							(0xFF & ((output[j] * image_alpha)/0xFF)) +
							(0xFF & ((background_r * background_alpha)/0xFF));
					}

					fb_addr_long[col] = 0x00FFFFFF &
						(output[j]<<16 |
						output[j]<<8 |
						output[j]);
				}
			}

			/* Grayscale with alpha, 8 bits per component */
			if (image_header.colour_type == COLOR_GREY_ALPHA &&
				image_header.bit_depth == 8) {

				if (output[j+1] & 0xFF){
					if ((output[j+1] & 0xFF) != 0xFF){
						image_alpha = output[j+1] & 0xFF;
						background_alpha = (0xFF - image_alpha) & 0xFF;

						output[j] =
							(0xFF & ((output[j] * image_alpha)/0xFF)) +
							(0xFF & ((background_r * background_alpha)/0xFF));
					}

					fb_addr_long[col] = 0x00FFFFFF &
						(output[j]<<16 |
						output[j]<<8 |
						output[j]);
				}
			}

			/* Grayscale, 16 OR 8 bits per component */
			if (image_header.colour_type == COLOR_GREY &&
				(image_header.bit_depth == 16 ||
				 image_header.bit_depth == 8)) {

				fb_addr_long[col] = 0x00FFFFFF &
					(output[j]<<16 |
					output[j]<<8 |
					output[j]);
			}

			/* Truecolor, 16 bits per component */
			if (image_header.colour_type == COLOR_TRUE &&
				image_header.bit_depth == 16) {

				fb_addr_long[col] = 0x00FFFFFF &
					(output[j]<<16 |
					output[j+2]<<8 |
					output[j+4]);
			}

			/* Truecolor, 8 bits per component */
			if (image_header.colour_type == COLOR_TRUE &&
				image_header.bit_depth == 8) {

				fb_addr_long[col] = 0x00FFFFFF &
					(output[j]<<16 |
					output[j+1]<<8 |
					output[j+2]);
			}

			/* Grayscale, 4 bits per component */
			if (image_header.colour_type == COLOR_GREY &&
				image_header.bit_depth == 4) {

				fb_addr_long[col] =
					CONV_GS_4_TO_32((output[j] & 0xF0)>>4);
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_4_TO_32(output[j] & 0x0F);
				}
			}

			/* Grayscale, 2 bits per component */
			if (image_header.colour_type == COLOR_GREY &&
				image_header.bit_depth == 2) {

				fb_addr_long[col] =
					CONV_GS_2_TO_32((output[j] & 0xC0) >> 6);

				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_2_TO_32((output[j] & 0x30) >> 4);
				}
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_2_TO_32((output[j] & 0x0C) >> 2);
				}
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_2_TO_32(output[j] & 0x03);
				}
			}

			/* Grayscale, 1 bit per component */
			if (image_header.colour_type == COLOR_GREY &&
				image_header.bit_depth == 1) {

				fb_addr_long[col] =
					CONV_GS_1_TO_32((output[j] & 0x80) >> 7);

				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_1_TO_32((output[j] & 0x40) >> 6);
				}
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_1_TO_32((output[j] & 0x20) >> 5);
				}
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_1_TO_32((output[j] & 0x10) >> 4);
				}
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_1_TO_32((output[j] & 0x08) >> 3);
				}
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_1_TO_32((output[j] & 0x04) >> 2);
				}
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_1_TO_32((output[j] & 0x02) >> 1);
				}
				if (col + 1 < image_header.width) {
					fb_addr_long[++col] =
						CONV_GS_1_TO_32(output[j] & 0x01);
				}
			}

			/* Palette, 8 bit per component */
			if (image_header.colour_type == COLOR_INDEXED &&
				image_header.bit_depth == 8) {

				small_color = output[j];
				fb_addr_long[col] = 0x00FFFFFF &
					image_palette[small_color];
			}

			/* Palette, 4 bit per component */
			if (image_header.colour_type == COLOR_INDEXED &&
				image_header.bit_depth == 4) {

				small_color = (output[j] & 0xF0) >> 4;
				fb_addr_long[col] = 0x00FFFFFF &
					image_palette[small_color];
				if (col + 1 < image_header.width) {
					small_color = output[j] & 0x0F;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
			}

			/* Palette, 2 bit per component */
			if (image_header.colour_type == COLOR_INDEXED &&
				image_header.bit_depth == 2) {

				small_color = (output[j] & 0xC0) >> 6;
				fb_addr_long[col] = 0x00FFFFFF &
					image_palette[small_color];
				if (col + 1 < image_header.width) {
					small_color = output[j] & 0x30 >> 4;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
				if (col + 1 < image_header.width) {
					small_color = output[j] & 0x0C >> 2;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
				if (col + 1 < image_header.width) {
					small_color = output[j] & 0x03;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
			}

			/* Palette, 1 bit per component */
			if (image_header.colour_type == COLOR_INDEXED &&
				image_header.bit_depth == 1) {

				small_color = (output[j] & 0x80) >> 7;
				fb_addr_long[col] = 0x00FFFFFF &
					image_palette[small_color];

				if (col + 1 < image_header.width) {
					small_color = (output[j] & 0x40) >> 6;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
				if (col + 1 < image_header.width) {
					small_color = (output[j] & 0x20) >> 5;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
				if (col + 1 < image_header.width) {
					small_color = (output[j] & 0x10) >> 4;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
				if (col + 1 < image_header.width) {
					small_color = (output[j] & 0x08) >> 3;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
				if (col + 1 < image_header.width) {
					small_color = (output[j] & 0x04) >> 2;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
				if (col + 1 < image_header.width) {
					small_color = (output[j] & 0x02) >> 1;
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
				if (col + 1 < image_header.width) {
					small_color = (output[j] & 0x01);
					fb_addr_long[++col] = 0x00FFFFFF &
						image_palette[small_color];
				}
			}

			j += bytes_pp;
			col++;
		}
		row++;
	}

	kfree(output);

}

/*
 * This is the function to decompress a huffman tree.
 *
 * @param stream (IN) This is the input data stream from which we are reading.
 * @param iter (IN/OUT) This is the input data stream's char iterator
 * @param bit_iter (IN/OUT) This is the bit iterator for the particular char we
 *                          are reading.
 * @param length_tree (IN) This is the huffman code's length tree.
 * @param distance_tree (IN) This is the huffman code's distance tree.
 * @param output (IN/OUT) This is an output stream to which we write out the
 *                        decompressed huffman data.
 * @param output_iter (IN/OUT) This is the output iterator.
 */
void decompress_huffman(
	unsigned char *stream,
	unsigned long *iter,
	unsigned char *bit_iter,
	huffman_node **length_tree,
	huffman_node **distance_tree,
	unsigned char *output,
	unsigned long *output_iter) {

	unsigned long j,k;
	huffman_node *final_node;
	unsigned long extra_value = 0;
	unsigned long length_value = 0;
	unsigned long distance_value = 0;

	/* Start going along the bitstream and traversing the tree
	 * until you get to a leaf
	 */
	get_huffman_code(stream, iter, bit_iter, length_tree, &final_node);

	while (final_node->value != 256) {

		if (final_node->value < 256){
			/* literal value */
			output[*output_iter] = final_node->value;
			(*output_iter)++;
		}
		if (final_node->value > 256){
			/* We have the initial length value,
			 * now get the extra length bits, if any
			 */
			extra_value = 0;
			length_value = 0;
			for (j=0; j<final_node->extra_bits; j++){
				extra_value = read_bit_from_stream(stream, iter, bit_iter);
				length_value += extra_value << j;
			}
			length_value += final_node->real;

			/* Now its time to get the distance value */
			get_huffman_code(stream, iter, bit_iter, distance_tree, &final_node);

			/* Get any extra bits for the distance value */
			extra_value = 0;
			distance_value = 0;
			for (j=0; j<final_node->extra_bits; j++){
				extra_value = read_bit_from_stream(stream, iter, bit_iter);
				distance_value += extra_value << j;
			}
			distance_value += final_node->real;

			/*
			 * Now we need to use the distance and length values
			 * to copy previously existing values
			 */
			distance_value = (*output_iter) - distance_value;
			for (k=0; k<length_value; k++){
				output[*output_iter] = output[distance_value];
				(*output_iter)++;
				distance_value++;
			}
		}

		/* Get the next code */
		get_huffman_code(stream, iter, bit_iter, length_tree, &final_node);
	}
}


/*
 * This is the function to build a static huffman tree.
 *
 * @param length_tree (OUT) This is the huffman code's length tree.
 * @param distance_tree (OUT) This is the huffman code's distance tree.
 */
void build_static_huffman_tree(
	huffman_node **length_tree,
	huffman_node **distance_tree) {

	huffman_node *new_node = 0;
	huffman_node *cur_node = 0;
	unsigned long j,k;
	unsigned long running_literal_value = 0;
	unsigned long running_real_value = 0;

	unsigned long ltree_literal_value[10] =
		{256,265,269,273,277,  0,280,281,285,144};
	unsigned long ltree_real_value[10] =
		{  2, 11, 19, 35, 67,  0,115,131,258,144};
	unsigned long ltree_literal_length[10] =
		{  9,  4,  4,  4,  3,144,  1,  4,  3,112};
	unsigned long ltree_code_start[10] =
		{  0,  9, 13, 17, 21, 48,192,193,197,400};
	unsigned long ltree_code_length[10] =
		{  7,  7,  7,  7,  7,  8,  8,  8,  8,  9};
	unsigned long ltree_extra_bits[10] =
		{  0,  1,  2,  3,  4,  0,  4,  5,  0,  0};

	unsigned long dtree_literal_value[15] =
		{0,4,6, 8,10,12, 14, 16, 18,  20,  22,  24,  26,   28,30};
	unsigned long dtree_real_value[15] =
		{1,5,9,17,33,65,129,257,513,1025,2049,4097,8193,16385, 0};
	unsigned long dtree_literal_length[15] =
		{4,2,2, 2, 2, 2,  2,  2,  2,   2,   2,   2,   2,    2, 2};
	unsigned long dtree_code_start[15] =
		{0,4,6, 8,10,12, 14, 16, 18,  20,  22,  24,  26,   28,30};
	unsigned long dtree_code_length[15] =
		{5,5,5, 5, 5, 5,  5,  5,  5,   5,   5,   5,   5,    5, 5};
	unsigned long dtree_extra_bits[15] =
		{0,1,2, 3, 4, 5,  6,  7,  8,   9,   10,  11, 12,   13, 0};

	/* Build our Huffman length tree using the fixed codes */
	new_node = (huffman_node *)kzalloc(sizeof(huffman_node), GFP_KERNEL);
    if (new_node == NULL) {
           return;
    }

	*length_tree = new_node;

	for (k=0; k<10; k++){
		running_literal_value = ltree_literal_value[k];
		running_real_value = ltree_real_value[k];
		for (j=0; j<ltree_literal_length[k]; j++) {
			new_node = (huffman_node *)kzalloc(
				sizeof(huffman_node), GFP_KERNEL);
            if (new_node == NULL) {
                   return;
            }

            new_node->extra_bits = (unsigned char)ltree_extra_bits[k];
			new_node->value = running_literal_value;
			new_node->real = running_real_value;
			cur_node = *length_tree;
			add_node(&cur_node,
				new_node,
				ltree_code_start[k] + j,
				ltree_code_length[k]);
			running_literal_value++;
			if (ltree_extra_bits[k]){
				running_real_value += (1<<ltree_extra_bits[k]);
			}else{
				running_real_value++;
			}
		}
	}

	/* Build our Huffman distance tree using the fixed codes */
	new_node = (huffman_node *)kzalloc(sizeof(huffman_node), GFP_KERNEL);
    if (new_node == NULL) {
           return;
    }
	*distance_tree = new_node;

	for (k=0; k<15; k++){
		running_literal_value = dtree_literal_value[k];
		running_real_value = dtree_real_value[k];
		for (j=0; j<dtree_literal_length[k]; j++) {
			new_node = (huffman_node *)kzalloc(
				sizeof(huffman_node), GFP_KERNEL);
            if (new_node == NULL) {
                   return;
            }
			new_node->extra_bits = (unsigned char)dtree_extra_bits[k];
			new_node->value = running_literal_value;
			new_node->real = running_real_value;
			cur_node = *distance_tree;
			add_node(&cur_node,
				new_node,
				dtree_code_start[k] + j,
				dtree_code_length[k]);
			running_literal_value++;
			if (dtree_extra_bits[k]){
				running_real_value += (1<<dtree_extra_bits[k]);
			}else{
				running_real_value++;
			}
		}
	}
}


/*
 * This is the function to build a dynamic huffman tree.
 *
 * @param stream (IN) This is the input data stream from which we are reading.
 * @param iter (IN/OUT) This is the input data stream's char iterator
 * @param bit_iter (IN/OUT) This is the bit iterator for the particular char we
 *                          are reading.
 * @param length_tree (OUT) This is the huffman code's length tree.
 * @param distance_tree (OUT) This is the huffman code's distance tree.
 */
void build_dynamic_huffman_tree(
	unsigned char *stream,
	unsigned long *iter,
	unsigned char *bit_iter,
	huffman_node **length_tree,
	huffman_node **distance_tree) {

	unsigned long j,k;
	unsigned long clc_order[19] =
		{16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
	unsigned long clc_lengths[19] =
		{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	unsigned long clc_extra_bits[19] =
		{2,3,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	unsigned long clc_values[19] =
		{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18};
	huffman_node *code_length_tree = 0;
	unsigned long dynamic_hlit = 0;
	unsigned long dynamic_hdist = 0;
	unsigned long dynamic_hclen = 0;

	unsigned long lit_extra_bits_num[LEN_NUM_DISTINCT_EXTRA_BITS] =
		{265,4,4,4,4,4,1};
	unsigned long lit_extra_bits[LEN_NUM_DISTINCT_EXTRA_BITS] =
		{0,1,2,3,4,5,0};
	unsigned long *dynamic_lit_code = 0;
	unsigned long *dynamic_lit_length = 0;
	unsigned long *dynamic_lit_extra_bits = 0;
	unsigned long *dynamic_lit_values = 0;
	unsigned long *dynamic_lit_real_values = 0;

	unsigned long dist_extra_bits_num[DIST_NUM_DISTINCT_EXTRA_BITS] =
		{4,2,2,2,2,2,2,2,2,2,2,2,2,2};
	unsigned long dist_extra_bits[DIST_NUM_DISTINCT_EXTRA_BITS] =
		{0,1,2,3,4,5,6,7,8,9,10,11,12,13};
	unsigned long *dynamic_dist_code = 0;
	unsigned long *dynamic_dist_length = 0;
	unsigned long *dynamic_dist_extra_bits = 0;
	unsigned long *dynamic_dist_values = 0;
	unsigned long *dynamic_dist_real_values = 0;

	unsigned long prev_real = 0;
	unsigned long code_index;
	huffman_node *new_node = 0;

	/* Read some initial information about our dynamic huffman tree */
	read_bits_from_stream(stream, iter, bit_iter, 5, &dynamic_hlit);
	read_bits_from_stream(stream, iter, bit_iter, 5, &dynamic_hdist);
	read_bits_from_stream(stream, iter, bit_iter, 4, &dynamic_hclen);

	dynamic_hlit += 257;
	dynamic_hdist++;
	dynamic_hclen += 4;

	/* Build our Huffman length tree using the fixed codes */
	new_node = (huffman_node *)kzalloc(sizeof(huffman_node), GFP_KERNEL);
    if (new_node == NULL) {
           return;
    }
	code_length_tree = new_node;

	/* Get the code lengths */
	for (k=0; k<19 && k<dynamic_hclen; k++){
		read_bits_from_stream(stream,
			iter, bit_iter,	3, &clc_lengths[clc_order[k]]);
	}

    /* build the code_length tree */
	if (create_tree(CLC_MAX_BITS, CLC_NUM_CODES,
		&clc_lengths[0],
		&clc_extra_bits[0],
		&clc_values[0],
		&clc_values[0],
        &code_length_tree) == 1) {
            EMGD_DEBUG("ERROR: create tree failed\n");
            return;
    }

	/* Build the literal/length alphabet */
	dynamic_lit_code = (unsigned long *)kzalloc(
		sizeof(unsigned long) * LEN_NUM_CODES, GFP_KERNEL);
    if (dynamic_lit_code == NULL) {
           return;
    }

	dynamic_lit_length = (unsigned long *)kzalloc(
		sizeof(unsigned long) * LEN_NUM_CODES, GFP_KERNEL);
    if (dynamic_lit_length == NULL) {
           return;
    }

	dynamic_lit_extra_bits = (unsigned long *)kzalloc(
		sizeof(unsigned long) * LEN_NUM_CODES, GFP_KERNEL);
    if (dynamic_lit_extra_bits == NULL) {
           return;
    }

	dynamic_lit_values = (unsigned long *)kzalloc(
		sizeof(unsigned long) * LEN_NUM_CODES, GFP_KERNEL);
    if (dynamic_lit_values == NULL) {
           return;
    }

	dynamic_lit_real_values = (unsigned long *)kzalloc(
		sizeof(unsigned long) * LEN_NUM_CODES, GFP_KERNEL);
    if (dynamic_lit_real_values == NULL) {
           return;
    }

	/* build extra information, such as extra bits, values and real_values */
	prev_real = 2;
	code_index = 0;
	for (k=0; k<LEN_NUM_DISTINCT_EXTRA_BITS; k++) {
		for (j=0; j<lit_extra_bits_num[k]; j++) {
			dynamic_lit_extra_bits[code_index] = lit_extra_bits[k];
			dynamic_lit_values[code_index] = code_index;

			if (code_index >= LEN_START_REAL_VALUES){
				dynamic_lit_real_values[code_index] =
					prev_real += (1<<dynamic_lit_extra_bits[code_index-1]);
			} else {
				dynamic_lit_real_values[code_index] = code_index;
			}
			code_index++;
		}
	}

	/* Doesn't seem to follow the pattern? */
	dynamic_lit_real_values[285] = 258;

    /* get code lengths for the literal/length alphabet */
	get_code_lengths(stream, iter, bit_iter, &code_length_tree,
		dynamic_hlit, dynamic_lit_length);

	/* allocate tree for literal/length codes */
	new_node = (huffman_node *)kzalloc(sizeof(huffman_node), GFP_KERNEL);
    if (new_node == NULL) {
           return;
    }
	*length_tree = new_node;

	/* build the literal/length tree */
    if (create_tree(LEN_MAX_BITS, LEN_NUM_CODES,
		dynamic_lit_length,
		dynamic_lit_extra_bits,
		dynamic_lit_values,
		dynamic_lit_real_values,
        length_tree) == 1) {
            EMGD_DEBUG("ERROR: create tree failed\n");
            return;
    }

	/* free all the literal/length data we are no longer using */
	kfree(dynamic_lit_code);
	kfree(dynamic_lit_length);
	kfree(dynamic_lit_extra_bits);
	kfree(dynamic_lit_values);
	kfree(dynamic_lit_real_values);


	/* Build the distance alphabet */
	dynamic_dist_code = (unsigned long *)kzalloc(
		sizeof(unsigned long) * DIST_NUM_CODES, GFP_KERNEL);
    if (dynamic_dist_code == NULL) {
           return;
    }

	dynamic_dist_length = (unsigned long *)kzalloc(
		sizeof(unsigned long) * DIST_NUM_CODES, GFP_KERNEL);
    if (dynamic_dist_length == NULL) {
           return;
    }

	dynamic_dist_extra_bits = (unsigned long *)kzalloc(
		sizeof(unsigned long) * DIST_NUM_CODES, GFP_KERNEL);
    if (dynamic_dist_extra_bits == NULL) {
           return;
    }

	dynamic_dist_values = (unsigned long *)kzalloc(
		sizeof(unsigned long) * DIST_NUM_CODES, GFP_KERNEL);
    if (dynamic_dist_values == NULL) {
           return;
    }

	dynamic_dist_real_values = (unsigned long *)kzalloc(
		sizeof(unsigned long) * DIST_NUM_CODES, GFP_KERNEL);
    if (dynamic_dist_real_values == NULL) {
           return;
    }

	/* build extra information, such as extra bits, values and real_values */
	prev_real = 1;
	code_index = 0;
	for (k=0; k<DIST_NUM_DISTINCT_EXTRA_BITS; k++) {
		for (j=0; j<dist_extra_bits_num[k]; j++) {
			dynamic_dist_extra_bits[code_index] = dist_extra_bits[k];
			dynamic_dist_values[code_index] = code_index;

			if (code_index >= DIST_START_REAL_VALUES){
				dynamic_dist_real_values[code_index] =
					prev_real += (1<<dynamic_dist_extra_bits[code_index-1]);
			} else {
				dynamic_dist_real_values[code_index] = code_index+1;
			}
			code_index++;
		}
	}

    /* get code lengths for the distance alphabet */
	get_code_lengths(stream, iter, bit_iter, &code_length_tree,
		dynamic_hdist, dynamic_dist_length);

	/* allocate tree for distance codes */
	new_node = (huffman_node *)kzalloc(sizeof(huffman_node), GFP_KERNEL);
	if (new_node == NULL) {
           return;
    }

	*distance_tree = new_node;

	/* build the distance tree */
	if (create_tree(DIST_MAX_BITS, DIST_NUM_CODES,
		&dynamic_dist_length[0],
		&dynamic_dist_extra_bits[0],
		&dynamic_dist_values[0],
		&dynamic_dist_real_values[0],
        distance_tree) == 1) {
            EMGD_DEBUG("ERROR: create tree failed.\n");
            return;
    }

	/* free all the distance data we are no longer using */
	kfree(dynamic_dist_code);
	kfree(dynamic_dist_length);
	kfree(dynamic_dist_extra_bits);
	kfree(dynamic_dist_values);
	kfree(dynamic_dist_real_values);

	/* All done with the code length tree, lets free this memory */
	free_node(code_length_tree);
}


/*
 * This is the function to get the dynamic code lengths for a specified
 * number of code lenths. There is some overuse of the word code and code
 * lengths, but thats sort of the way the PNG spec is.  This is because
 * we use codes to decode compressed codes.
 *
 * @param stream (IN) This is the input data stream from which we are reading.
 * @param iter (IN/OUT) This is the input data stream's char iterator
 * @param bit_iter (IN/OUT) This is the bit iterator for the particular char we
 *                          are reading.
 * @param code_length_tree (IN) This is the huffman code length tree, which is
 *                              used to get the code lengths.
 * @param num_lengths (IN) The number of code lengths.
 * @param dynamic_lengths (OUT) Gets the dynamic length for the different
 *                              code lengths
 */
void get_code_lengths(
	unsigned char *stream,
	unsigned long *iter,
	unsigned char *bit_iter,
	huffman_node **code_length_tree,
	unsigned long num_lengths,
	unsigned long *dynamic_lengths) {

	unsigned long j,k;
	huffman_node *final_node;
	unsigned long dynamic_repeat_length = 0;

	/* get code lengths for the literal/length alphabet */
	for (k=0; k<num_lengths; k++) {
		get_huffman_code(stream, iter, bit_iter,
			code_length_tree, &final_node);

		if (final_node->value < 16){
			dynamic_lengths[k] = final_node->value;
		} else {
			switch (final_node->value) {
			case 16:
				/* get repeat length */
				read_bits_from_stream(stream,
					iter, bit_iter, 2, &dynamic_repeat_length);
				dynamic_repeat_length += 3;
				for (j=0; j<dynamic_repeat_length; j++){
					dynamic_lengths[k+j] = dynamic_lengths[k-1];
				}
				k += j-1;
				break;
			case 17:
				/* get repeat length */
				read_bits_from_stream(stream,
					iter, bit_iter, 3, &dynamic_repeat_length);
				dynamic_repeat_length += 3;
				for (j=0; j<dynamic_repeat_length; j++){
					dynamic_lengths[k+j] = 0;
				}
				k += j-1;
				break;
			case 18:
				/* get repeat length */
				read_bits_from_stream(stream,
					iter, bit_iter, 7, &dynamic_repeat_length);
				dynamic_repeat_length += 11;
				for (j=0; j<dynamic_repeat_length; j++){
					dynamic_lengths[k+j] = 0;
				}
				k += j-1;
				break;
			}
		}
	}
}


/*
 * This function creates a tree given the necessary tree information.
 *
 * @param max_bits (IN) The maximum number of bits for any code
 * @param num_codes (IN) The number of codes
 * @param code_lengths (IN) The code lengths
 * @param extra_bits (IN) The number of extra bits for each huffman code
 * @param values (IN) The values for the huffman code
 * @param real_values (IN) The real values for the huffman code
 * @param tree (OUT) The resulting huffman tree.
 *
 * @return 0 on Success
 * @return >0 on Error
 */
int create_tree(
	unsigned long max_bits,
	unsigned long num_codes,
	unsigned long *code_lengths,
	unsigned long *extra_bits,
	unsigned long *values,
	unsigned long *real_values,
	huffman_node **tree) {

	unsigned long *clc_count;
	unsigned long *clc_next_code;
	unsigned long *codes;
	unsigned long clc_code;
	unsigned long k;
	huffman_node *cur_node;
	huffman_node *new_node;

	if (!tree) {
		EMGD_DEBUG("Bad tree pointer.");
		return 1;
	}

	/* Step 1: Count the number of codes for each code length */
	clc_count = (unsigned long *)kzalloc(
		sizeof(unsigned long) * (max_bits+1), GFP_KERNEL);
    if (clc_count == NULL) {
           return 1;
    }

	for (k=0; k<num_codes; k++){
		clc_count[code_lengths[k]]++;
	}

	/* Step 2: Get numerical value of smallest code for each code length */
	clc_next_code = (unsigned long *)kzalloc(
		sizeof(unsigned long) * (max_bits+1), GFP_KERNEL);
    if (clc_next_code == NULL) {
           return 1;
    }

	clc_code = 0;
	clc_next_code[0] = 2;
	for (k=1; k<=max_bits; k++){
	    clc_code = (clc_code + clc_count[k-1]) << 1;
	    clc_next_code[k] = clc_code;
	}

	/* Step 3: Assign numerical values to all codes */
	codes = (unsigned long *)kzalloc(
		sizeof(unsigned long) * num_codes, GFP_KERNEL);
    if (codes == NULL) {
           return 1;
    }

	for (k=0; k<num_codes; k++){
	    if (code_lengths[k] > 0){
			codes[k] = clc_next_code[code_lengths[k]]++;

	        /* Add this node to the code length tree */
	        new_node = (huffman_node *)kzalloc(
				sizeof(huffman_node), GFP_KERNEL);
            if (new_node == NULL) {
                   return 1;
            }

	        new_node->extra_bits = (unsigned char)extra_bits[k];
	        new_node->value = values[k];
	        new_node->real = real_values[k];
	        cur_node = *tree;
	        add_node(&cur_node, new_node, codes[k], code_lengths[k]);
	    }
	}

	kfree(clc_count);
	kfree(clc_next_code);
	kfree(codes);
	return 0;
}


/*
 * This function recursively frees a huffman node and all its sub nodes.
 * First we free any sub nodes, then we free itself.
 *
 * @param node (IN) The huffman node to free.
 */
void free_node(huffman_node *node) {
	if (node->leaf[0]) {
		free_node((huffman_node *)(node->leaf[0]));
	}
	if (node->leaf[1]) {
		free_node((huffman_node *)(node->leaf[1]));
	}
	kfree(node);
}


/*
 * This function gets a huffman code by traversing through a bit
 * stream as if those are directions for traversling through
 * a binary tree.   When we hit a leaf node, we have our value.
 *
 * @param stream (IN) This is the input data stream from which we are reading.
 * @param iter (IN/OUT) This is the input data stream's char iterator
 * @param bit_iter (IN/OUT) This is the bit iterator for the particular char we
 *                          are reading.
 * @param tree (IN) This is the huffman tree.
 * @param final_node (OUT) The final leaf node we have reached.
 */
void get_huffman_code(
	unsigned char *stream,
	unsigned long *iter,
	unsigned char *bit_iter,
	huffman_node **tree,
	huffman_node **final_node){

	*final_node = *tree;
	while ((*final_node)->leaf[0] || (*final_node)->leaf[1]) {
		(*final_node) = (huffman_node *)(*final_node)->
			leaf[((stream[*iter] >> *bit_iter) & 1)];

		if (++(*bit_iter) == 8) {
			(*iter)++;
			(*bit_iter) = 0;
		}
	}
}


/*
 * This function adds a node into a tree.
 *
 * @param tree (IN/OUT) This is the tree's root to which we'll be adding a
 *                      node.
 * @param node (IN) This is the node we'll be adding.
 * @param code (IN) This is the code which will be used as a map to determine
 *                  where the new node goes on the tree.
 * @param code_length (IN) This is the code length for the code passed in.
 *
 * @return 0 on Success
 * @return >0 on Error
 */
int add_node(
	huffman_node **tree,
	huffman_node *node,
	unsigned long code,
	unsigned long code_length){

	huffman_node *new_node;

	if (!(*tree)) {
		EMGD_DEBUG("Invalid tree pointer.");
		return 1;
	}

	if (code_length > 1){

		/* Build a leaf node if it doesn't exist */
		if (!(*tree)->leaf[(code >> (code_length-1)) & 1]){
			new_node = (huffman_node *)kzalloc(
				sizeof(huffman_node), GFP_KERNEL);
            if (new_node == NULL) {
                   return 1;
            }

			(*tree)->leaf[(code >> (code_length-1)) & 1] =
				(struct huffman_node *)new_node;
			(*tree) = new_node;
		} else {
			(*tree) =
				(huffman_node *)(*tree)->leaf[(code >> (code_length-1)) & 1];
		}

		/* Recursively add the tree node */
		add_node(&(*tree), node, code, --code_length);

	} else {
		/* This is where our leaf node belongs */
		(*tree)->leaf[code & 1] = (struct huffman_node *)node;
	}
	return 0;
}


/*
 * This function reads a 4 byte value from a given stream.
 * This assumes the passed in stream is byte aligned.
 *
 * @param stream (IN) The stream from which we are reading.
 * @param iter (IN/OUT) The stream iterator.
 * @param value (OUT) The value read from the stream.
 *
 * @return 0 on Success
 * @return >0 on Error
 */
int read_int_from_stream(
	unsigned char *stream,
	unsigned long *iter,
	unsigned long *value){

	*value = stream[*iter] << 24 |
		stream[(*iter)+1] << 16 |
		stream[(*iter)+2] << 8 |
		stream[(*iter)+3];
	*iter += 4;
	return 0;
}


/*
 * This function reads a 1 byte value from a given stream.
 * This assumes the passed in stream is byte aligned.
 *
 * @param stream (IN) The stream from which we are reading.
 * @param iter (IN/OUT) The stream iterator.
 * @param value (OUT) The value read from the stream.
 *
 * @return 0 on Success
 * @return >0 on Error
 */
int read_char_from_stream(
	unsigned char *stream,
	unsigned long *iter,
	unsigned char *value){

	*value = stream[*iter];
	(*iter)++;

	return 0;
}


/*
 * This function reads a given number of bits from a given stream.
 * This does not assume the passed in stream is byte aligned.
 *
 * @param stream (IN) The stream from which we are reading.
 * @param iter (IN/OUT) The stream iterator.
 * @param bit_iter (IN/OUT) The stream's bit iterator.
 * @param num_bits (IN) The number of bits to read.
 * @param value (OUT) The value read from the stream.
 *
 * @return 0 on Success
 * @return >0 on Error
 */
int read_bits_from_stream(
	unsigned char *stream,
	unsigned long *iter,
	unsigned char *bit_iter,
	unsigned long num_bits,
	unsigned long *value){

	unsigned long i;
	*value = 0;

	for (i=0; i<num_bits; i++){
		*value += read_bit_from_stream(stream, iter, bit_iter) << i;
	}

	return 0;
}


/*
 * This function reads a single bit from a given stream.
 * This does not assume the passed in stream is byte aligned.
 *
 * @param stream (IN) The stream from which we are reading.
 * @param iter (IN/OUT) The stream iterator.
 * @param bit_iter (IN/OUT) The stream's bit iterator.
 *
 * @return The bit value read.
 */
unsigned int read_bit_from_stream(
	unsigned char *stream,
	unsigned long *iter,
	unsigned char *bit_iter){

	unsigned int result = 0;

	/* get our bit */
	result = (stream[*iter] >> *bit_iter) & 1;

	/* This is faster than above */
	if (++(*bit_iter) == 8) {
		(*iter)++;
		(*bit_iter) = 0;
	}

	return result;
}
