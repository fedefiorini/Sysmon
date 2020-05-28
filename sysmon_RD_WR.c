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
#define ITERATIONS		200
/* No. of pages */
#define PAGES_TOTAL  	300000
/* Sampling interval */
#define TIME_INTERVAL	30
/* Ranges of page no. */
#define VH			200
#define H				150
#define M				100
#define L   		64
#define VL_MAX	10
#define VL_MIN	5
/* Constant for RD/WR history array (taken directly from old code) */
#define HIST_ARR	400
/**************************************/

/* Variables declaration */
struct timer_list stimer;
long long page_heat[PAGES_TOTAL];
long long shadow[PAGES_TOTAL];	//??
int page_counts;
int random_page;
int sampling_interval;
int w2r, r2w;	//??
int loops;
int reuse_time[ITERATIONS];
int dirty_page[ITERATIONS];
int rd_times[PAGES_TOTAL];
int wr_times[PAGES_TOTAL];
int out_data[PAGES_TOTAL];
int pg_rd_times[PAGES_TOTAL];
int pg_wr_times[PAGES_TOTAL];
int history[HIST_ARR][PAGES_TOTAL];

int highr, highw, midhigh, mid, midlow;	//??

static int process_id;
module_param(process_id, int, S_IRUGO|S_IWUSR); //Define process_id as module parameter
/*************************/

/* Memory Access Functions */
static int scan_pgtable(void);
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

	random_page = 50;
	loops				= 0;

  return 0;
}

static void __exit timer_exit(void)
{
	if (loops != 0)
	{
		int i, j;
		for (i = 0; i < PAGES_TOTAL; i++)
		{
			pg_rd_times[i] = 0;
			pg_wr_times[i] = 0;
		}
		highr = 0, highw = 0, midhigh = 0, mid = 0, midlow = 0;
		for (i = 0; i < loops; i++)
		{
			for (j = 0; j < PAGES_TOTAL; j++)
			{
				if (history[i][j] == 1) pg_rd_times[j]++;
				if (history[i][j] == 2) pg_wr_times[j]++;
			}
		}
		for (i = 0; i < PAGES_TOTAL; i++)
		{
			if ((pg_rd_times[i] == 0) && (pg_wr_times[i] != 0))
			{
				highw++;
				continue;
			}
			if ((pg_rd_times[i] != 0) && (pg_wr_times[i] == 0))
			{
				highr++;
				continue;
			}
			/* FP Operations are not allowed. Rounding up without losing too much precision */
			if ((int)10*(pg_rd_times[i] / pg_wr_times[i]) > 20)
			{
				midhigh++;
				continue;
			}
			if ((int)10*(pg_rd_times[i] / pg_wr_times[i]) < 20 &&
				(int)10*(pg_rd_times[i] / pg_wr_times[i]) > 5)
			{
				mid++;
				continue;
			}
			if ((int)10*(pg_rd_times[i] / pg_wr_times[i]) < 5)
			{
				midlow++;
				continue;
			}
		}
		/********************OUTPUT********************/
		printk("[LOG]: After Sampling...\n");
		printk("These values denote RD/WR patterns\n");
		printk("-->Only RD is %d. ", highr);
		printk("-->Only WR is %d. ", highw);
		printk("-->RD/WR in (2,--) is %d. ", midhigh);
		printk("-->RD/WR in [0.5, 2] is %d. Denotes RD :=: WR. ", mid);
		printk("-->RD/WR in (0, 0.5) is %d. Denotes RD << WR. \n", midlow);
		/**********************************************/
	}

  del_timer(&stimer);
  printk("SysMon: Module Unloaded!\n");

  return;
}
/***********************************************/

/* Memory scanning implementation */
static int scan_pgtable(void)
{
  /* Current process */
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
  int num_hot_pages = 0;  // No. of hot pages
  int num_vpages    = 0;
  int cycle_index   = 0;  // Loop counter
  int hot_pages[ITERATIONS];
  int i             = 0;

  /* Temporary counters */
  int pg_cnt        = 0;
  int num_cur_pg    = 0;
	int times 				= 0;

  /* Page "heat" variables */
  int h             = 0;
  int m             = 0;
  int l             = 0;
  int l2            = 0;
  int l3            = 0;
  int l4            = 0;

  int all_pages     = 0;  // Total no. of pages
  int avg_hotpage   = 0;  // Avg no. of hot pages (per iteration)
  int num_access    = 0;  // Total no. of memory accesses (all pages)
  int avg_pg_util   = 0;  // Avg utilization for each page

	/* RD / WR Patterns variables */
	int ri						= 0;
	int wi            = 0;

  if ((proc = get_current()) == NULL)
  {
    // Something went wrong...
    printk("SysMon: Get No Process Handle. Exit and Try Again \n");
    return 0;
  }
  else
  {
    //mm = proc->mm; changed to active_mm (kernel thread)
    mm = proc->active_mm;
    if (mm == NULL)
    {
      // Something went wrong...
      printk("SysMon: MM is NULL. Exit and Try Again \n");
      return 0;
    }

    for (i = 0; i < PAGES_TOTAL; i++)
		{
			page_heat[i] 			= -1;
			rd_times[i] 			= 0;
			wr_times[i]				= 0;
			history[loops][i] = 0;
		}
    for (i = 0; i < ITERATIONS; i++)
		{
			hot_pages[i] 	= 0;
			dirty_page[i] = 0;
			reuse_time[i] = 0;
		}

		times = 0;
    for (cycle_index = 0; cycle_index < ITERATIONS; cycle_index++)
    {
      // Initialize the no. of hot pages to zero at every loop
      num_hot_pages = 0;

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

							// Reset and clear Dirty bit
							pte = pte_mkclean(pte);
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
					page_counts++;
        }
      }

      // Count no. of hot pages
      num_vpages = 0;

			sampling_interval = page_counts / 110;
			page_counts 			= 0;

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
              hot_pages[cycle_index]++;

							if (page_counts == random_page) times++;
							if (pte_dirty(pte))
							{
								wr_times[num_cur_pg]++;
								if (page_counts == random_page) dirty_page[cycle_index]++;
							}
							else rd_times[num_cur_pg]++;
            }
						else
						{
							if (page_counts == random_page) reuse_time[times]++;
						}
          }
          pg_cnt++;
					page_counts++;
          pte_unmap_unlock(ptep, ptl);
        }
        num_vpages += (int)((end - start) / PAGE_SIZE);
      }
    }

		random_page += sampling_interval;
		if (random_page >= page_counts) random_page = (int) page_counts / 300;

    /******************************OUTPUT******************************/
    for (i = 0; i < PAGES_TOTAL; i++)
    {
      if (page_heat[i] < VH && page_heat[i] > H) h++;
      if (page_heat[i] <= H && page_heat[i] > M) m++;
      if (page_heat[i] <= M && page_heat[i] > L) l++;
      if (page_heat[i] <= L && page_heat[i] > VL_MAX) l2++;
      if (page_heat[i] <= VL_MAX && page_heat[i] > VL_MIN) l3++;
      if (page_heat[i] <= VL_MIN && page_heat[i] > 0) l4++;
      if (page_heat[i] > -1) all_pages++;
    }

    /* Print results on screen (dmesg output - syslog) */
    printk("[LOG]: After %d sampling loops ", ITERATIONS);
    printk("this is the result of the physical page accessing frequence\n");
    printk("H(150,200): %d\n", h);
    printk("M(100,150]: %d\n", m);
    printk("L(64,100]: %d\n",  l);
    printk("LL(10,64]: %d\n",  l2);
    printk("LLL(5,10]: %d\n",  l3);
    printk("LLLL(1,5]: %d\n",  l4);

    /* Calculate average number of hot pages per iteration */
    for (i = 0; i < ITERATIONS; i++)
    {
      avg_hotpage += hot_pages[i];
      avg_hotpage /= (i + 1);
    }

    /* Print results for memory accesses across all pages */
    for (i = 0; i < ITERATIONS; i++)
    {
      if (page_heat[i] > -1) num_access += (page_heat[i] + 1);
    }
    printk("[LOG]: The number of accesses is %d, on average %d \n", num_access, num_access / ITERATIONS);
    avg_pg_util = num_access / all_pages;

    printk("[LOG]: The average number of hot pages is %d, ", avg_hotpage);
    printk("the average utilization per page is %d ", avg_pg_util);
    printk("and the number of used pages is %d\n", all_pages);

		if ((times == 0) && (reuse_time[0] == 0)) printk("[LOG]: Page no. %d is not available", random_page);
		else
		{
			if ((times == 0) && (reuse_time[0] == 0)) printk("Page no. %d was not used in this %d loops",
																								random_page, ITERATIONS);
			else
			{
				if (times < ITERATIONS) times++;
				printk("The Re-Use Time of page no. %d is ", random_page);
				for (i = 0; i < times; i++) printk("%d", reuse_time[i]);
				printk("\n");
			}
		}

		/* Print information about reading and writing patterns */
		w2r = 0, r2w = 0, ri = 0, wi = 0;
		for (i = 0; i < PAGES_TOTAL; i++)
		{
			if (rd_times[i] > 2 * wr_times[i])
			{
				if (out_data[i] == 2) w2r++;

				out_data[i] 			= 1;
				history[loops][i] = 1;
				ri++;
				continue;
			}
			else
			{
				if (wr_times[i] > 0)
				{
					if (out_data[i] == 1) r2w++;

					out_data[i]				= 2;
					history[loops][i] = 2;
					wi++;
				}
			}
		}
		loops++;

		printk("[LOG]: The no. of RD-dominant pages is %d. \n", ri);
		printk("[LOG]: The no. of WR-dominant pages is %d. \n", wi);
		printk("The no. of pages RD-->WR is %d. ", r2w);
		printk("The no. of pages WR-->RD is %d. \n", w2r);
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
