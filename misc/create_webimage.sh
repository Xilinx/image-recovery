#******************************************************************************
# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
# SPDX-License-Identifier: MIT
#******************************************************************************

rm -f web.img

# Create image file of 96K
dd if=/dev/zero of=web.img bs=512 count=192

# Format image with FAT
/sbin/mkfs.vfat -F 12 web.img

# Copy webpages using mtools (no mount/sudo needed)
mcopy -i web.img -s ./web_pages/* ::
chmod 444 web.img
