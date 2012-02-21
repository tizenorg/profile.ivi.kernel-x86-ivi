#----------------------------------------------------------------------------
# Filename: Makefile.gnu
# $Revision: 1.1.2.1 $
#----------------------------------------------------------------------------
# INTEL CONFIDENTIAL
# Copyright (2002-2008) Intel Corporation All Rights Reserved.
# The source code contained or described herein and all documents related to
# the source code ("Material") are owned by Intel Corporation or its suppliers
# or licensors. Title to the Material remains with Intel Corporation or its
# suppliers and licensors. The Material contains trade secrets and proprietary
# and confidential information of Intel or its suppliers and licensors. The
# Material is protected by worldwide copyright and trade secret laws and
# treaty provisions. No part of the Material may be used, copied, reproduced,
# modified, published, uploaded, posted, transmitted, distributed, or
# disclosed in any way without Intel's prior express written permission.
#
# No license under any patent, copyright, trade secret or other intellectual
# property right is granted to or conferred upon you by disclosure or
# delivery of the Materials, either expressly, by implication, inducement,
# estoppel or otherwise. Any license under such intellectual property rights
# must be express and approved by Intel in writing.
#
#----------------------------------------------------------------------------
DRV     = ch7036
SOURCES = \
            ch7036_port.c \
            ch7036_intf.c \
            ch7036_attr.c \
			ch7036_fw.c \
            ch7036.c \
            ch7036_iic.c \
            ch7036_pm.c \
            ch7036_reg_table.c \
            lvds.c


include ../Makefile.include

#----------------------------------------------------------------------------
# File Revision History
# $Id: Makefile.gnu,v 1.1.2.1 2011/09/13 08:50:22 nanuar Exp $
# $Source: /nfs/fm/proj/eia/cvsroot/koheo/linux.nonredistributable/ch7036/ch7036pd_src/Attic/Makefile.gnu,v $
#----------------------------------------------------------------------------

