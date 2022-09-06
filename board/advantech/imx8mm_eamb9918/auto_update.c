/*
 * Copyright 2022 Advantech
 */

#include <ctype.h>
#include <mmc.h>
#include <fs.h>


#ifdef CONFIG_ADV_RECOVERY	
#define SWUPDATE_GENERAL_STRING_SIZE	256
#define MAX_IMAGE_FNAME	SWUPDATE_GENERAL_STRING_SIZE

/*
 * cpio header - swupdate does not
 * support images generated with ancient cpio.
 * Just the new format as described in cpio
 * documentation is supported.
 */

struct new_ascii_header
{
  char c_magic[6];
  char c_ino[8];
  char c_mode[8];
  char c_uid[8];
  char c_gid[8];
  char c_nlink[8];
  char c_mtime[8];
  char c_filesize[8];
  char c_dev_maj[8];
  char c_dev_min[8];
  char c_rdev_maj[8];
  char c_rdev_min[8];
  char c_namesize[8];
  char c_chksum[8];
};

struct filehdr {
	unsigned long size;
	unsigned long namesize;
	unsigned long chksum;
	char filename[MAX_IMAGE_FNAME];
};

#define SW_DESCRIPTION_FILENAME	 "sw-description"

static unsigned long from_ascii (char const *where, size_t digs, unsigned int logbase)
{
	unsigned long value = 0;
	char const *buf = where;
	char const *end = buf + digs;
	int overflow = 0;
	static char codetab[] = "0123456789ABCDEF";

	for (; *buf == ' '; buf++)
	{
		if (buf == end)
		return 0;
	}

	if (buf == end || *buf == 0)
		return 0;
	
	while (1) {
		unsigned int d;
		char *p = strchr (codetab, toupper (*buf));
		if (!p) {
			printf("Malformed number %.*s\n", (int)digs, where);
			break;
		}

		d = p - codetab;
		if ((d >> logbase) > 1) {
			printf("Malformed number %.*s\n", (int)digs, where);
			break;
		}
		value += d;
		if (++buf == end || *buf == 0)
			break;
		overflow |= value ^ (value << logbase >> logbase);
		value <<= logbase;
	}
	if (overflow)
		printf("Archive value %.*s is out of range\n", (int)digs, where);
	return value;
}

static int get_cpiohdr(unsigned char *buf, unsigned long *size,
			unsigned long *namesize, unsigned long *chksum)
{
	struct new_ascii_header *cpiohdr;

	if (!buf) {
		printf("buf is error!\n");
		return -1;
	}

	cpiohdr = (struct new_ascii_header *)buf;
	if (strncmp(cpiohdr->c_magic, "070702", 6) != 0) {
		printf("CPIO Format not recognized: magic not found\n");
		return -1;
	}
	*size = from_ascii(cpiohdr->c_filesize, sizeof(cpiohdr->c_filesize), 4);
	*namesize = from_ascii(cpiohdr->c_namesize, sizeof(cpiohdr->c_namesize), 4);
	*chksum = from_ascii(cpiohdr->c_chksum, sizeof(cpiohdr->c_chksum), 4);
	return 0;
}

static int fill_buffer_block(unsigned char *buf, unsigned int nbytes, unsigned long *offs)
{
	loff_t actread;
	unsigned long count = 0;
    
	while (nbytes > 0) {
		if (fs_set_blk_dev("mmc", "1:1", FS_TYPE_FAT)) {
			printf("fs_set_blk_dev error!\n");
			return -1;
		}
		
		if (fs_read("/swupdate-image.swu", buf, *offs, nbytes, &actread) < 0) {
			printf("fs_read error\n");
			return -1;
		} 
		
		if(actread == 0) {
			return -1;
		} 
			
		buf += actread;
		nbytes -= actread;
		*offs += actread;
		count += actread;
	}

	return count;
}

static int extract_cpio_header( struct filehdr *fhdr, unsigned long *offset)
{
	unsigned char buf[sizeof(fhdr->filename)];
	if (fill_buffer_block(buf, sizeof(struct new_ascii_header), offset) < 0) {
		printf("CPIO Header fill_buffer failed\n");
		return -1;
	}
	
	if (get_cpiohdr(buf, &fhdr->size, &fhdr->namesize, &fhdr->chksum) < 0) {
		printf("CPIO Header corrupted, cannot be parsed\n");
		return -1;
	}

	if (fhdr->namesize >= sizeof(fhdr->filename)) {
		printf("CPIO Header filelength too big %u >= %u (max)\n",
			(unsigned int)fhdr->namesize,
			(unsigned int)sizeof(fhdr->filename));
		return -1;
	}
	
	if (fill_buffer_block(buf, fhdr->namesize, offset) < 0) {
		printf("CPIO Header filename fill_buffer failed\n");
		return -1;
	}
	
	buf[fhdr->namesize] = '\0';
	strlcpy(fhdr->filename, (char *)buf, sizeof(fhdr->filename));
	return 0;
}

static int get_image_length()
{
	loff_t size;
	int ret = -1;
	
	if (fs_set_blk_dev("mmc", "1:1", FS_TYPE_FAT )) {
		printf("fs_set_blk_dev error!\n");
		return -1;
	}

	ret = fs_size("/swupdate-image.swu", &size);
	if (ret) {
		debug("fs_size error!\n");
		return -1;
	}
	
	if (size == 0) {
		return -1;
	}

	ret = size;
	return ret;
}

static int check_swupdate_image()
{
	struct filehdr fdh;
	unsigned long offset = 0;

	if (get_image_length() < 0) {
		return -1;
	}

	if (extract_cpio_header(&fdh, &offset) < 0) {
		return -1;
	}

	if (strcmp(fdh.filename, SW_DESCRIPTION_FILENAME)) {
		printf("description file name not the first of the list: %s instead of %s\n",
			fdh.filename,
			SW_DESCRIPTION_FILENAME);
		return -1;
	}
	return 0;
}

void detect_sdcard_autoupdate()
{
	int mmc_dev_no = 1;
	struct mmc *mmc;
	
	mmc = find_mmc_device(mmc_dev_no);
	if (!mmc) {
		printf("no mmc device at slot %x\n", mmc_dev_no);
		return;
	}

	//Check sdcard update file exits, Enter recovery
	if ((mmc_getcd(mmc) == 1) && (file_exists("mmc", "1:1", "swupdate-image.swu", FS_TYPE_FAT) == 1)) {
		int ret = check_swupdate_image();
		if( ret == 0 ) {
			env_set("recovery_status", "in_progress");
			env_update("mmcargs", "sdfwupdate");
		} else {
			printf("swupdate image file not valid\n");
		}
	}
	return;
}
#endif

