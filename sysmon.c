/**
* author:	Federico Fiorini
* date:		21-May-2020
*
* This version recalls the original work from L.Liu et alii.
* Modifications should work with newer versions of Linux kernel (tested on
* v4.15.0, Ubuntu 18.04 LTS). Retro-compatibility yet to be tested.
*
* This version includes changes to module initialization and memory management.
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/sem.h>
#include <linux/list.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/writeback.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/kallsyms.h>
#include <linux/swapops.h>
#include <linux/elf.h>
#include <linux/version.h>


/* INFO: Adjust constants as required */
#define TIME_INTERVAL 30
/**************************************/

/* Variables declaration */
struct timer_list stimer;

static int process_id;
module_param(process_id, int, S_IRUGO | S_IWUSR);
/*************************/

/* Retrieve the process interested to monitor */
static struct task_struct* get_process(void)
{
  struct pid* pid;

  /* Check that process_id has been passed correctly */
  if (process_id != 0)
  {
    pid = find_vpid(process_id);

    return pid_task(pid, PIDTYPE_PID);
  }
  else return NULL;
}
/**********************************************/

/* Memory Access Functions */
static int get_result(void)
{
  
}
/***************************/

/* Timer functions (init, exit, timer handler) */
static void timer_handler(struct timer_list *aTimer)
{
  int res = 0;
  mod_timer(&stimer, jiffies + (TIME_INTERVAL * HZ));

  res = get_result();
  if (!res) printk("SysMon: Something Went Wrong\n");

  printk("Module Working\n");
}

static int __init timer_init(void)
{
  timer_setup(&stimer, timer_handler, 0);
  stimer.function = timer_handler;
  stimer.expires  = jiffies + (TIME_INTERVAL * HZ);

  add_timer(&stimer);
  printk("SysMon: Module Init!\n");

  return 0;
}

static void __exit timer_exit(void)
{
  del_timer(&stimer);
  printk("SysMon: Module Unloaded!\n");

  return;
}
/***********************************************/

/* Module configuration */
module_init(timer_init);
module_exit(timer_exit);
MODULE_AUTHOR("ffiorini");
MODULE_LICENSE("GPL");
/************************/
