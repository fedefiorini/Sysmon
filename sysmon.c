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
/* No. of sampling loops */
#define ITERATIONS		50
/* Sampling interval */
#define TIME_INTERVAL	15
/**************************************/

/* Variables declaration */
struct timer_list stimer;

static int process_id;
module_param(process_id, int, S_IRUGO|S_IWUSR); //Define process_id as module parameter
/*************************/

/* Memory Access Functions */
static int scan_pgtable(void);
static struct task_struct *get_process(void);
/***************************/

/* Timer functions (init, exit, timer handler) */
static void timer_handler(struct timer_list *aTimer)
{
  int found = 0;

  mod_timer(&stimer, jiffies + (TIME_INTERVAL * HZ));

  found = scan_pgtable();
  if (!found)
  {
    /* Failed to get some results in scanning pages.
        Something must've gone wrong... */
    printk("Profiling: Scanning Page Table Failed....\n");
  }
}

static int __init timer_init(void)
{
  timer_setup(&stimer, timer_handler, 0);
  stimer.function = timer_handler;
  stimer.expires  = jiffies + (TIME_INTERVAL * HZ);

  add_timer(&stimer);
  printk("Profiling Module Loaded!\n");

  return 0;
}

static void __exit timer_exit(void)
{
  del_timer(&stimer);
  printk("Profiling Module Unloaded!\n");

  return;
}
/***********************************************/

/* Memory scanning implementation */
static struct task_struct * get_process(void)
{
  struct pid *pid = NULL;
  struct task_struct *process = NULL;

  if (process_id != 0)
  {
    /* Find PID structure using the PID number */
    pid = find_vpid(process_id);
    /* Retrieve the process from its PID */
    process = get_pid_task(pid, PIDTYPE_PID);
  }
  return process;
}

static int scan_pgtable(void)
{
  /* Process profiled */
  struct task_struct *process;

  /* Page Table Walk */
  /* 5-level implemented (unused levels are folded at runtime) */
  pgd_t *pgd  = NULL;
  p4d_t *p4d  = NULL;
  pud_t *pud  = NULL;
  pmd_t *pmd  = NULL;
  pte_t *ptep = NULL;
  pte_t pte;

  spinlock_t *lock;

  /* Process' memory management variables */
  struct mm_struct *mm;
  struct vm_area_struct *vma;

  unsigned long address = 0;
  unsigned long start   = 0;
  unsigned long end     = 0;

  /* Report variables and counters */
  int cycle_index   = 0;
  int i             = 0;
  long num_vpages   = 0;

  if ((process = get_process()) == NULL)
  {
    /* Something went wrong looking for the process */
    printk("Profiling: Can't retrieve process. Exit and Retry...\n");
    return 0;
  }
  else
  {
    mm = process->mm;
    if (mm == NULL)
    {
      /* Something went wrong accessing process' memory */
      printk("Profiling: MM is NULL. Exit and Retry...\n");
      return 0;
    }
    else
    {
      /* PROFILING: PAGE WALK AND SAMPLING OF ACCESS_BIT */
      for (cycle_index = 0; cycle_index < ITERATIONS; cycle_index++)
      {
          /* Scan each vma */
          for (vma = mm->mmap; vma; vma = vma->vm_next)
          {
            start = vma->vm_start;
            end   = vma->vm_end;
            mm    = vma->vm_mm;

            /* Scan all pages belonging to the vma */
            for (address = start; address < end; address += PAGE_SIZE)
            {
              /* Scan Page Tables */
              pgd = pgd_offset(mm, address);
              if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd))) continue;
              p4d = p4d_offset(pgd, address);
              if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d))) continue;
              pud = pud_offset(p4d, address);
              if (pud_none(*pud) || unlikely(pud_bad(*pud))) continue;
              pmd = pmd_offset(pud, address);
              if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd))) continue;

              ptep  = pte_offset_map_lock(mm, pmd, address, &lock);
              pte   = *ptep;

              if (pte_present(pte))
              {
                num_vpages++;   /* Increase page counter */
                if (pte_young(pte))
                {
                  /* Hot Page. Clear access bit */
                  pte = pte_mkold(pte);
                  set_pte_at(mm, address, ptep, pte);
                  pte_unmap_unlock(ptep, lock);
                }
              }
              else
              {
                pte_unmap_unlock(pte, lock);
                continue;
              }
            } /* for address */
          } /* for vma */
      } /* for iterations */

      printk("Number of virtual pages: %ld\n", num_vpages);
    } /* if mm */
  } /* if process */

  return 1;
} /* end of function */
/**********************************/

/* Module configuration */
module_init(timer_init);
module_exit(timer_exit);
MODULE_AUTHOR("ffiorini");
MODULE_LICENSE("GPL");
/************************/
