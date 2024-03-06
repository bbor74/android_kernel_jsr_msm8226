#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/export.h>
#include <misc/app_info.h>

#include <mach/msm_smsm.h>

#define APP_NAME_VALUE_SPLIT_CHAR  "*"
#define SENSOR_MAX 4

#define SUMSUNG_ID   1
#define ELPIDA_ID     3
#define HYNIX_ID     6

static struct kobject *set_appinfo_node_kobject = NULL;

static unsigned int sensors_bitmap = 0;
static char *sensor_name[SENSOR_MAX] = {
	"G-Sensor",
	"M-Sensor",
	"LP-Sensor",
	"V-Gyro-Sensor",
};

void sensors_set_bit(int nr, int *addr)
{
	if (NULL != addr)
		*addr |= (1<<nr);
}

bool sensors_test_bit(int nr, int *addr)
{
	if (NULL != addr)
		return (*addr)&(1<<nr) ? true : false;
	else
		return false;
}

struct info_node
{
    char name[APP_INFO_NAME_LENTH];
    char value[APP_INFO_VALUE_LENTH];
    struct list_head entry;
};

typedef struct
{
    unsigned int lpddrID;                  /* DDR ID */
    unsigned int update_flag[2];           /* sd auto update flag */
    unsigned char sb_seme_data[16];
    /* add the huawei_debug to match the aboot & sbl */
    unsigned int huawei_debug;
    unsigned int reserved;                  /* reserved for filling */
} smem_exten_huawei_paramater;

static LIST_HEAD(app_info_list);
static DEFINE_SPINLOCK(app_info_list_lock);

/*
 * Name and value separation character: *
 * Example buf: LP-Sensor*avago 9930
 *   name: LP-Sensor
 *   value: avago 9930
 */
static ssize_t hw_set_app_info_node_store(struct kobject *dev,
		struct kobj_attribute *attr, const char *buf, size_t size)
{
	char* AppStr = NULL;
	char AppName[APP_INFO_NAME_LENTH] = { 0 };
	char AppValue[APP_INFO_VALUE_LENTH] = { 0 };
	int name_lenth = 0, value_lenth = 0, buf_len = 0, ret = -1, idx = 0;
	char* strtok = NULL;

	buf_len = strlen(buf);
	AppStr = kmalloc(buf_len+5, GFP_KERNEL);
	if (!AppStr)
	{
		pr_err("%s: kmalloc fail!", __func__);
		return -1;
	}

	memset(AppStr, 0, buf_len + 5);
	sprintf(AppStr, "%s", buf);
	strtok = strsep(&AppStr, APP_NAME_VALUE_SPLIT_CHAR);
	if (strtok != NULL)
	{
		name_lenth = strlen(strtok);
		memcpy(AppName, strtok, (name_lenth > (APP_INFO_NAME_LENTH-1)
				? (APP_INFO_NAME_LENTH-1) : name_lenth));
	}
	else
	{
		pr_err("%s: buf name Invalid: %s", __func__, buf);
		goto split_fail_exit;
	}
	strtok = strsep(&AppStr, APP_NAME_VALUE_SPLIT_CHAR);
	if (strtok != NULL)
	{
		value_lenth = strlen(strtok);
		memcpy(AppValue, strtok, (value_lenth > (APP_INFO_VALUE_LENTH-1)
				? (APP_INFO_VALUE_LENTH-1) : value_lenth));
	}
	else
	{
		pr_err("%s: buf value Invalid:%s", __func__, buf);
		goto split_fail_exit;
	}

	for (idx = 0; idx < SENSOR_MAX; idx++) {
		if (!strncmp(AppName, sensor_name[idx],
				strlen(sensor_name[idx]))) {
			if (sensors_test_bit(idx, &sensors_bitmap)) {
				goto split_fail_exit;
			} else {
				sensors_set_bit(idx,  &sensors_bitmap);
			}
			break;
		}
	}

	ret = app_info_set(AppName, AppValue);
	if (ret < 0)
	{
		pr_err("%s: app_info_set fail", __func__);
		goto split_fail_exit;
	}

split_fail_exit:
	if (AppStr)
		kfree(AppStr);
	return size;
}

static struct kobj_attribute sys_set_appinfo_init = {
	.attr = {
		.name = "set_app_info_node",
		.mode = (S_IRUGO | S_IWUSR | S_IWGRP),
	},
	.show = NULL,
	.store = hw_set_app_info_node_store,
};

static int app_info_node_init(void)
{
	int err = -100;
	set_appinfo_node_kobject = kobject_create_and_add("set_app_info", NULL);
	if (!set_appinfo_node_kobject)
	{
		pr_err("%s: create set_app_info folder error!", __func__);
		return -1;
	}

	err = sysfs_create_file(set_appinfo_node_kobject,
				&sys_set_appinfo_init.attr);
	if (err)
	{
		pr_err("%s: init set_appinfo_node_kobject file fail", __func__);
		return -1;
	}

	return 1;
}

int app_info_set(const char * name, const char * value)
{
    struct info_node *new_node = NULL;
    int name_lenth = 0;
    int value_lenth = 0;

    if(WARN_ON(!name || !value))
        return -1;

    name_lenth = strlen(name);
    value_lenth = strlen(value);

    new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
    if(new_node == NULL)
    {
        return -1;
    }

    memcpy(new_node->name,name,((name_lenth > (APP_INFO_NAME_LENTH-1))?(APP_INFO_NAME_LENTH-1):name_lenth));
    memcpy(new_node->value,value,((value_lenth > (APP_INFO_VALUE_LENTH-1))?(APP_INFO_VALUE_LENTH-1):value_lenth));

    spin_lock(&app_info_list_lock);
    list_add_tail(&new_node->entry,&app_info_list);
    spin_unlock(&app_info_list_lock);

    return 0;
}

EXPORT_SYMBOL(app_info_set);


static int app_info_proc_show(struct seq_file *m, void *v)
{
    struct info_node *node;

    list_for_each_entry(node,&app_info_list,entry)
    {
        seq_printf(m,"%-32s:%32s\n",node->name,node->value);
    }
    return 0;
}

static int app_info_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, app_info_proc_show, NULL);
}

static const struct file_operations app_info_proc_fops =
{
    .open		= app_info_proc_open,
    .read		= seq_read,
    .llseek		= seq_lseek,
    .release	= single_release,
};

/*
    Function to read the SMEM to get the lpDDR name
*/
void export_ddr_name(unsigned int ddr_vendor_id)
{
    char * ddr_info = NULL;
    char *SUMSUNG_DDR = "SUMSUNG";
    char *ELPIDA_DDR = "ELPIDA";
    char *HYNIX_DDR  = "HYNIX";

     switch (ddr_vendor_id)
     {
        case SUMSUNG_ID:
        {
            ddr_info = SUMSUNG_DDR;
            break;
        }
        case ELPIDA_ID:
        {
            ddr_info = ELPIDA_DDR;
            break;
        }
        case HYNIX_ID:
        {
            ddr_info = HYNIX_DDR;
            break;
        }
        default:
        {
            ddr_info = "UNKNOWN";
            break;
        }
     }

    /* Set the vendor name in app_info */
    app_info_set("ddr_vendor", ddr_info);

    /* Print the DDR Name in the kmsg log */
    pr_err("DDR VENDOR NAME is : %s", ddr_info);

    return;
}


void app_info_print_smem(void)
{
    unsigned int ddr_vendor_id = 0;
    /* read share memory and get DDR ID */
    smem_exten_huawei_paramater *smem = NULL;

    smem = smem_alloc(SMEM_ID_VENDOR1, sizeof(smem_exten_huawei_paramater));
    if(NULL == smem)
    {
        /* Set the vendor name in app_info */
        app_info_set("ddr_vendor", "UNKNOWN");

        pr_err("%s: SMEM Error, READING DDR VENDOR NAME", __func__);
        return;
    }

    ddr_vendor_id = smem->lpddrID;
    ddr_vendor_id &= 0xff;

    export_ddr_name(ddr_vendor_id);

    return;
}

static int __init proc_app_info_init(void)
{
    proc_create("app_info", 0, NULL, &app_info_proc_fops);

    app_info_print_smem();
    app_info_node_init();

    return 0;
}

module_init(proc_app_info_init);
