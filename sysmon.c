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
#define ITERATIONS		100
/* No. of pages */
#define PAGES_TOTAL  	1000000
/* Sampling interval */
#define TIME_INTERVAL	15
/* Ranges of page no. */
#define VH			200
#define H				150
#define M				100
#define L   		64
#define VL_MAX	10
#define VL_MIN	5      // Check these values...
/**************************************/

/* Variables declaration */
struct timer_list stimer;
int page_heat[PAGES_TOTAL];

static int process_id;
module_param(process_id, int, S_IRUGO|S_IWUSR); //Define process_id as module parameter
/*************************/

/* Memory Access Functions */
static int scan_pgtable(void);
static task_struct *get_process(void);
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
    printk("SysMon: Scanning Page Table Failed....\n");
  }
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

/* Memory scanning implementation */
static task_struct *get_process(void)
{
  struct pid *pid;

  /* Check that process_id has been passed correctly */
  if (process_id != 0)
  {
    pid = find_vpid(process_id);
  }

  return pid_task(pid, PIDTYPE_PID);
}

static int scan_pgtable(void)
{
  /* Benchmark (profiled) process */
  struct task_struct *proc;

  /* Page Table levels (implemented 5-level) */
  pgd_t *pgd = NULL;
  p4d_t *p4d = NULL;
  pud_t *pud = NULL;
  pmd_t *pmd = NULL;
  pte_t *ptep, pte;

  spinlock_t *ptl;

  /* Memory management variables */
  struct mm_struct *mm;
  struct vm_area_struct *vma;
  unsigned long start   = 0;
  unsigned long end     = 0;
  unsigned long address = 0;

  /* Memory report variables */
  int num_vpages    = 0;
  int cycle_index   = 0;  // Loop counter
  // int hot_pages[ITERATIONS];
  int i             = 0;
  //
  // /* Temporary counters */
  int pg_cnt        = 0;
  int num_cur_pg    = 0;
  //
  // /* Page "heat" variables */
  // int h             = 0;
  // int m             = 0;
  // int l             = 0;
  // int l2            = 0;
  // int l3            = 0;
  // int l4            = 0;
  //
  // int all_pages     = 0;  // Total no. of pages
  // int avg_hotpage   = 0;  // Avg no. of hot pages (per iteration)
  // int num_access    = 0;  // Total no. of memory accesses (all pages)
  // int avg_pg_util   = 0;  // Avg utilization for each page

  if ((proc = get_process()) == NULL)
  {
    // Something went wrong...
    printk("SysMon: Get No Process Handle. Exit and Try Again \n");
    return 0;
  }
  else
  {
    mm = proc->mm;
    if (mm == NULL)
    {
      // Something went wrong...
      printk("SysMon: MM is NULL. Exit and Try Again \n");
      return 0;
    }

    for (i = 0; i < PAGES_TOTAL; i++) page_heat[i] = -1;
    // for (i = 0; i < ITERATIONS; i++) hot_pages[i] = 0;

    for (cycle_index = 0; cycle_index < ITERATIONS; cycle_index++)
    {
      // Start scanning each VMA
      for (vma = mm->mmap; vma; vma = vma->vm_next)
      {
        start = vma->vm_start;
        end   = vma->vm_end;
        mm    = vma->vm_mm;

        /* Check all pages for each VMA */
        for (address = start; address < end; address += PAGE_SIZE)
        {
          /* Scan page table levels for each page */
          pgd = pgd_offset(mm, address);
          if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
            continue;
          p4d = p4d_offset(pgd, address);
          if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d)))
            continue;
          pud = pud_offset(p4d, address);
          if (pud_none(*pud) || unlikely(pud_bad(*pud)))
            continue;
          pmd = pmd_offset(pud, address);
          if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
            continue;

          ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
          pte  = *ptep;
          if (pte_present(pte))
          {
            if (pte_young(pte))
            {
              // Hot Page. Reset and clear Access bit
              pte = pte_mkold(pte);
              set_pte_at(mm, address, ptep, pte);
            }
          }
          else
          {
            // No Page
            pte_unmap_unlock(ptep, ptl);
            continue;
          }
          pte_unmap_unlock(ptep, ptl);
        } /* for (address) */
      } /* for (vma) */

      // Count no. of hot pages
      num_vpages = 0;
      i = 0;
      for (vma = mm->mmap; vma; vma = vma->vm_next)
      {
        start   = vma->vm_start;
        end     = vma->vm_end;
        mm      = vma->vm_mm;

        pg_cnt = 0;
        for (address = start; address < end; address += PAGE_SIZE)
        {
          /* Scan page table levels for each page */
          pgd = pgd_offset(mm, address);
          if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
            continue;
          p4d = p4d_offset(pgd, address);
          if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d)))
            continue;
          pud = pud_offset(p4d, address);
          if (pud_none(*pud) || unlikely(pud_bad(*pud)))
            continue;
          pmd = pmd_offset(pud, address);
          if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
            continue;

          ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
          pte  = *ptep;

          if (pte_present(pte))
          {
            if (pte_young(pte))
            {
              // Hot Pages
              num_cur_pg = pg_cnt + num_vpages;
              page_heat[num_cur_pg]++;
              // hot_pages[cycle_index]++;
            }
          }
          pg_cnt++;

          pte_unmap_unlock(ptep, ptl);
        } /* for (address) */
        i++;
        num_vpages += (int)((end - start) / PAGE_SIZE);
        printk("Num_vpages %d vma %d\n", num_vpages, i);
      } /* for (vma) */
    } /* for (iterations) */
    /******************************OUTPUT******************************/
    /******************************************************************/

    return 1;
  }
}
/**********************************/

/* Module configuration */
module_init(timer_init);
module_exit(timer_exit);
MODULE_AUTHOR("ffiorini");
MODULE_LICENSE("GPL");
/************************/
