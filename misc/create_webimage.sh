#******************************************************************************
# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
# SPDX-License-Identifier: MIT
#******************************************************************************

IMG_FILE="${1:-web.img}"

rm -f "$IMG_FILE"

# Create image file of 96K
dd if=/dev/zero of="$IMG_FILE" bs=512 count=192

# Format image with FAT
mkfs.vfat -F 12 "$IMG_FILE"

# Copy webpages using mtools (no mount/sudo needed)
mcopy -i "$IMG_FILE" -s ./web_pages/* ::
chmod 444 "$IMG_FILE"
