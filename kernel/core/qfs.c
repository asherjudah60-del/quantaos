#include <stdint.h>
#include <quanta/arch.h>
#include <quanta/vfs.h>

#define QFS_LBA 258U
#define QFS_SECTOR 512U
#define QFS_RECORD_SIZE 64U
#define QFS_PATH_SIZE 48U
#define QFS_FILE 1U

struct qfs_superblock { char magic[8]; uint16_t version; uint16_t sector_size; uint32_t count; uint32_t table_lba; uint32_t data_lba; uint32_t total_sectors; uint32_t checksum; uint32_t reserved; } __attribute__((packed));
struct qfs_record { char path[QFS_PATH_SIZE]; uint32_t lba; uint32_t size; uint32_t sectors; uint32_t flags; } __attribute__((packed));
static uint8_t sector[QFS_SECTOR];
static void copy_bytes(void *destination, const void *source, uint32_t length) { uint8_t *out=destination; const uint8_t *in=source; while(length--) *out++=*in++; }
static uint32_t crc32(const uint8_t *data, uint32_t length) { uint32_t crc=0xffffffffU,index,bit; for(index=0;index<length;++index){crc^=data[index];for(bit=0;bit<8U;++bit)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));}return ~crc; }
static int text_equal(const char *left, const char *right) { while(*left&&*right&&*left==*right){++left;++right;}return *left==*right; }
static uint32_t text_length(const char *text) { uint32_t length=0; while(text[length]) ++length; return length; }
static int starts_with(const char *text, const char *prefix) { while(*prefix && *text == *prefix) { ++text; ++prefix; } return *prefix == 0; }
static int listed(const char *output, const char *name) { uint32_t index=0,name_length=text_length(name); while(output[index]) { uint32_t start=index,offset=0; while(output[index] && output[index]!=' ' && output[index]!='\n') ++index; while(offset<name_length&&output[start+offset]==name[offset]) ++offset; if(offset==name_length&&offset==index-start) return 1; while(output[index]==' '||output[index]=='\n') ++index; } return 0; }
static int read_sector(uint32_t sector_number) {
	return quanta_block_device_read(quanta_storage_boot_device(), sector_number, sector);
}
static int load_super(struct qfs_superblock *super) { if(read_sector(QFS_LBA)!=0)return -1;copy_bytes(super,sector,sizeof(*super));if(super->magic[0]!='Q'||super->magic[1]!='F'||super->magic[2]!='S'||super->version!=1U||super->sector_size!=QFS_SECTOR||super->count==0U||super->table_lba<QFS_LBA+1U||super->data_lba<super->table_lba)return -1;if(super->checksum!=crc32(sector,28U))return -1;return 0; }
int quanta_qfs_read(const char *path, char *output, uint32_t capacity) { struct qfs_superblock super;struct qfs_record record;uint32_t index,offset,copied;if(path==0||output==0||capacity==0U||load_super(&super)!=0)return -1;for(index=0;index<super.count;++index){uint32_t record_offset=index*QFS_RECORD_SIZE;uint32_t record_lba=super.table_lba+record_offset/QFS_SECTOR;uint32_t record_byte=record_offset%QFS_SECTOR;if(read_sector(record_lba)!=0)return -1;copy_bytes(&record,sector+record_byte,sizeof(record));if(record.flags==QFS_FILE&&text_equal(record.path,path)){if(record.size>=capacity||record.sectors==0U||record.lba<super.data_lba)return -1;copied=0;for(offset=0;offset<record.sectors&&copied<record.size;++offset){uint32_t count=record.size-copied;if(count>QFS_SECTOR)count=QFS_SECTOR;if(read_sector(record.lba+offset)!=0)return -1;for(uint32_t byte=0;byte<count;++byte)output[copied+byte]=(char)sector[byte];copied+=count;}output[copied]=0;return (int)copied;}}return -1;}
int quanta_qfs_list(const char *path, char *output, uint32_t capacity) { struct qfs_superblock super;struct qfs_record record;uint32_t index,offset,prefix_length,used=0;char prefix[QFS_PATH_SIZE];if(path==0||output==0||capacity<2U||load_super(&super)!=0)return -1;output[0]=0;if(path[0]=='/'&&path[1]==0){prefix[0]='/';prefix[1]=0;}else{text_length(path);for(prefix_length=0;path[prefix_length]&&prefix_length+1U<QFS_PATH_SIZE;++prefix_length)prefix[prefix_length]=path[prefix_length];if(prefix_length==0||prefix[prefix_length-1U]!='/')prefix[prefix_length++]='/';prefix[prefix_length]=0;}prefix_length=text_length(prefix);for(index=0;index<super.count;++index){uint32_t record_offset=index*QFS_RECORD_SIZE;uint32_t record_lba=super.table_lba+record_offset/QFS_SECTOR;uint32_t record_byte=record_offset%QFS_SECTOR;uint32_t name_length=0;char name[QFS_PATH_SIZE];if(read_sector(record_lba)!=0)return -1;copy_bytes(&record,sector+record_byte,sizeof(record));if(record.flags!=QFS_FILE||!starts_with(record.path,prefix)||record.path[prefix_length]==0)continue;while(record.path[prefix_length+name_length]&&record.path[prefix_length+name_length]!='/') {name[name_length]=record.path[prefix_length+name_length];++name_length;}name[name_length]=0;if(listed(output,name))continue;if(used+name_length+2U>=capacity)return -1;for(offset=0;offset<name_length;++offset)output[used++]=name[offset];output[used++]=' ';output[used]=0;}if(used==0)return -1;output[used-1U]='\n';output[used]=0;return (int)used;}
int quanta_qfs_stat(const char *path, struct quanta_file_stat *stat) {
	struct qfs_superblock super; struct qfs_record record; uint32_t index;
	char listing[QFS_PATH_SIZE];
	if (path == 0 || stat == 0 || load_super(&super) != 0) return -1;
	for (index = 0; index < super.count; ++index) {
		uint32_t record_offset = index * QFS_RECORD_SIZE;
		if (read_sector(super.table_lba + record_offset / QFS_SECTOR) != 0) return -1;
		copy_bytes(&record, sector + record_offset % QFS_SECTOR, sizeof(record));
		if (record.flags == QFS_FILE && text_equal(record.path, path)) {
			stat->inode = index + 1U; stat->size = record.size;
			stat->blocks = record.sectors; stat->mode = 0644U;
			stat->uid = 1000U; stat->gid = 1000U; stat->type = QUANTA_FILE_REGULAR;
			stat->generation = 1U; return 0;
		}
	}
	if (quanta_qfs_list(path, listing, sizeof(listing)) >= 0) {
		stat->inode = 0; stat->size = 0; stat->blocks = 0; stat->mode = 0755U;
		stat->uid = 1000U; stat->gid = 1000U; stat->type = QUANTA_FILE_DIRECTORY;
		stat->generation = 1U; return 0;
	}
	return -1;
}
static const struct quanta_fs_provider qfs_v1_provider = {
	"qfs1", quanta_qfs_read, quanta_qfs_list, quanta_qfs_stat
};

const struct quanta_fs_provider *quanta_qfs_v1_provider(void) {
	return &qfs_v1_provider;
}

uint64_t quanta_qfs_v1_size_bytes(void) {
	struct qfs_superblock super;
	if (load_super(&super) != 0) return 0;
	return (uint64_t)super.total_sectors * QFS_SECTOR;
}