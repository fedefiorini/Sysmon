/**
* author: liulei2010@ict.ac.cn
* @20130629
* sequentially scan the page table to check and re-new __access_bit, and cal. the number of hot pages.
* add shadow count index
*
* Modifications@20150130 recover from an unknown problem. This version works well.
*
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
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/writeback.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/kallsyms.h>
#include <linux/swapops.h>
#include <linux/elf.h>
#include <linux/sched.h>

#include <linux/version.h>

struct timer_list stimer;
static int scan_pgtable(void);
static struct task_struct * traver_all_process(void);
long long shadow1[300000];

//write by yanghao
int page_counts;//to record number of used page.
int reuse_time[200];//to record each 200 loops the reuse_distance of one page.
int random_page;//the No. of page being monitored.
int sampling_interval;//random_page's sampling interval.
int dirty_page[200];//to record the writting informations about random_page.
int read_times[300000];//the reading times of each page.
int write_times[300000];//the writting times of each page.
int out_data[300000];
int w2r,r2w;
int history[400][300000];//the history of RD and WD
int loops;
int page_read_times[300000];
int page_write_times[300000];
int highr_yanghao,highw_yanghao,midhigh_yanghao,mid_yanghao,midlow_yanghao;
//end


static int process_id;
module_param(process_id, int, S_IRUGO|S_IWUSR);

/**
* begin to cal. the number of hot pages.
* And we will re-do it in every TIME_INTERVAL seconds.
*
* Not sure if change will work..
*/
static void time_handler(struct timer_list* stimer)
{
	int win=0;
     	mod_timer(stimer, jiffies + 5*HZ);
     	win = scan_pgtable(); /* 1 is win.*/
     	if(!win) /* we get no page, maybe something wrong occurs.*/
        	printk("sysmon: fail in scanning page table...\n");
}

static int __init timer_init(void)
{
	printk("sysmon: module init!\n");

	random_page = 50;
  loops = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0))
			__init_timer(&stimer, time_handler, 0);
			stimer.function = time_handler; //make time_handler correctly?
#else
     	init_timer(&stimer);
     	stimer.data = 0;
			stimer.function = time_handler;
#endif
     	stimer.expires = jiffies + 5*HZ;

     	add_timer(&stimer);
     	return 0;
}

static void __exit timer_exit(void)
{
     //yanghao:
     if (loops != 0)
     {
         int j,i;
         for (j=0; j<300000; j++)
         {
             page_read_times[j] = 0;
             page_write_times[j] = 0;
         }
         highr_yanghao=0, highw_yanghao=0, midhigh_yanghao=0, mid_yanghao=0, midlow_yanghao=0;
         for (i=0; i<loops; i++)
         {
             for (j=0; j<300000; j++)
             {
                 if (history[i][j] == 1)
                     page_read_times[j]++;
                 if (history[i][j] == 2)
                     page_write_times[j]++;
             }
         }
         for (j=0; j<300000; j++)
         {
             if (page_read_times[j]==0 && page_write_times[j]!=0)
             {
                 highw_yanghao++;
                 continue;
             }
             if (page_read_times[j]!=0 && page_write_times[j]==0)
             {
                 highr_yanghao++;
                 continue;
             }
             if ((int)page_read_times[j]/page_write_times[j] > 2)
             {
                 midhigh_yanghao++;
                 continue;
             }
             if ((int)page_read_times[j]/page_write_times[j] < 2
                && (int)2*(page_read_times[j]/page_write_times[j]) > 1)
             {
                 mid_yanghao++;
                 continue;
             }
             if ((int)2*(page_read_times[j]/page_write_times[j]) < 1)
                 midlow_yanghao++;
         }
         printk("[LOG]after sampling ...\n");
         printk("the values denote RD/WD.\n");
         printk("-->only RD is %d.\n--> only WD is %d.\n-->RD/WD locates in (2,--) is %d. Indicate RD >> WD.\n-->RD/WD locates in [0.5,2] is %d. Indicate RD :=: WD.\n-->RD/WD locates in (0,0.5) is %d. Indicate RD << WD.\n", highr_yanghao, highw_yanghao, midhigh_yanghao, mid_yanghao, midlow_yanghao);
     }//end yanghao
     printk("Unloading leiliu module.\n");
     del_timer(&stimer);//delete the timer
     return;
}

#if 1
//get the process of current running benchmark. The returned value is the pointer to the process.
static struct task_struct * traver_all_process(void)
{
	struct pid * pid;
  	pid = find_vpid(process_id);
  	return pid_task(pid,PIDTYPE_PID);
}
#endif

#if 1
//pgtable sequential scan and count for __access_bits
static int scan_pgtable(void)
{
     pgd_t *pgd = NULL;
     pud_t *pud = NULL;
     pmd_t *pmd = NULL;
     pte_t *ptep, pte;
     spinlock_t *ptl;

     //unsigned long tmp=0; // used in waiting routine
     struct mm_struct *mm;
     struct vm_area_struct *vma;
     unsigned long start=0, end=0, address=0;
     int number_hotpages = 0, number_vpages=0;
     int tmpp;
     int hot_page[200];
     struct task_struct *bench_process = traver_all_process(); //get the handle of current running benchmark
     int j, times;

     if(bench_process == NULL)
     {
          printk("leiliu: get no process handle in scan_pgtable function...exit&trying again...\n");
          return 0;
     }
     else // get the process
          mm = bench_process->mm;
     if(mm == NULL)
     {
          printk("leiliu: error mm is NULL, return back & trying...\n");
          return 0;
     }

     j=0;
     for(;j<300000;j++)
     {
        shadow1[j]=-1;

        //yanghao
        read_times[j] = 0;
        write_times[j] = 0;
        history[loops][j] = 0;
        //end
     }
     for(j=0;j<=199;j++)
     {
        hot_page[j]=0;
        //yanghao
        dirty_page[j] = 0;
        reuse_time[j] = 0;
     }

     //yanghao
     times = 0;

     //printk("re-set shadow\n");
     for(tmpp=0;tmpp<200;tmpp++)
     {
       number_hotpages = 0;
       //scan each vma
       for(vma = mm->mmap; vma; vma = vma->vm_next)
       {
          start = vma->vm_start;
          end = vma->vm_end;
          mm = vma->vm_mm;
          //in each vma, we check all pages
          for(address = start; address < end; address += PAGE_SIZE)
          {
              //scan page table for each page in this VMA
              pgd = pgd_offset(mm, address);
              if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
                  continue;
                  #if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
                  		     /* Adjusted to match 5-level page table implementation */
                  		     pud = pud_offset((p4d_t*) pgd, address);
                  #else
                  		     pud = pud_offset(pgd, address);
                  #endif
              if (pud_none(*pud) || unlikely(pud_bad(*pud)))
                  continue;
              pmd = pmd_offset(pud, address);
              if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
                  continue;
              ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
              pte = *ptep;
              if(pte_present(pte))
              {
                  if(pte_young(pte)) // hot page
                  {
                      //re-set and clear  _access_bits to 0
                      pte = pte_mkold(pte);
                      set_pte_at(mm, address, ptep, pte);
                      //yanghao:re-set and clear _dirty_bits to 0
                      pte = pte_mkclean(pte);
                      set_pte_at(mm, address, ptep, pte);
                  }
              }
              pte_unmap_unlock(ptep, ptl);
              page_counts++;
          } // end for(adddress .....)
       } // end for(vma ....)
        //5k instructions in idle
       // for(tmp=0;tmp<200*5;tmp++) {;} //1k instructions = 200 loops. 5 instructions/per loop.

        //count the number of hot pages
        if(bench_process == NULL)
        {
           printk("leiliu1: get no process handle in scan_pgtable function...exit&trying again...\n");
           return 0;
        }
        else // get the process
           mm = bench_process->mm;
        if(mm == NULL)
        {
           printk("leiliu1: error mm is NULL, return back & trying...\n");
           return 0;
        }
        number_vpages = 0;

        sampling_interval = page_counts/110;//yanghao:
        page_counts = 0;

        for(vma = mm->mmap; vma; vma = vma->vm_next)
        {
           start = vma->vm_start;
           end = vma->vm_end;
           //scan each page in this VMA
           mm = vma->vm_mm;
           int pos=0;
           for(address = start; address < end; address += PAGE_SIZE)
           {
               //scan page table for each page in this VMA
               pgd = pgd_offset(mm, address);
               if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
                     continue;
                     #if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
                     		     /* Adjusted to match 5-level page table implementation */
                     		     pud = pud_offset((p4d_t*) pgd, address);
                     #else
                     		     pud = pud_offset(pgd, address);
                     #endif
               if (pud_none(*pud) || unlikely(pud_bad(*pud)))
                     continue;
               pmd = pmd_offset(pud, address);
               if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
                     continue;
               ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
               pte = *ptep;
               if(pte_present(pte))
               {
                     if(pte_young(pte)) // hot pages
                     {
                           int now = pos + number_vpages;
                           //shadow[now]++;
                           shadow1[now]++;
                           hot_page[tmpp]++;
                           //yanghao:
                           if (page_counts == random_page)
                           {
                               times++;
                           }
                           if (pte_dirty(pte))
                           {
                               write_times[now]++;
                               if (page_counts == random_page)
                               {
                                   dirty_page[tmpp] = 1;
                               }
                           }
                           else
                               read_times[now]++;

                     }
                     else
                     {
                           if (page_counts == random_page)
                               reuse_time[times]++;
                     }//end
               }
               pos++;
               pte_unmap_unlock(ptep, ptl);
               page_counts++;
           } //end for(address ......)
           number_vpages += (int)(end - start)/PAGE_SIZE;
         } // end for(vma .....) */
      } //end 200 times repeats
      //yanghao:cal. the No. of random_page
      random_page += sampling_interval;
      if(random_page >= page_counts)
           random_page=page_counts/300;
//////////////////////////////////////////////
      //output after 200 times
/*      foo=0;
      for(j=0;j<30*10000;j++)
      {
          if(shadow1[j]>-1)
              foo++;
      }
*/
      int avg_page_utilization, avg_hotpage, num_access;
      int hig, mid, low, llow, lllow, llllow, all_pages;
      int ri, wi;

      hig=0,mid=0,low=0,llow=0,lllow=0,llllow=0,all_pages=0;
      for(j=0;j<30*100*100;j++)
      {
         if(shadow1[j]<200 && shadow1[j]>150)
              hig++;
         if(shadow1[j]>100 && shadow1[j]<=150)
              mid++;
         if(shadow1[j]<=100 && shadow1[j]>64)
              low++;
         if(shadow1[j]>10 && shadow1[j]<=64)
              llow++;
         if(shadow1[j]>5 && shadow1[j]<=10)
              lllow++;
         if(shadow1[j]>=0 && shadow1[j]<=5)
              llllow++;
         if(shadow1[j]>-1)
              all_pages++;
      }

      //the values reflect the accessing frequency of each physical page.
      printk("[LOG: after sampling (200 loops) ...] ");
      printk("the values denote the physical page accessing frequence.\n");
      printk("-->hig (150,200) is %d. Indicating the number of re-used pages is high.\n-->mid (100,150] is %d.\n-->low (64,100] is %d.\n-->llow (10,64] is %d. In locality,no too many re-used pages.\n-->lllow (5,10] is %d.\n-->llllow [1,5] is %d.\n", hig, mid, low, llow, lllow, llllow);


      avg_hotpage=0; //the average number of hot pages in each iteration.
      for(j=0;j<200;j++)
         avg_hotpage+=hot_page[j];
      avg_hotpage/=(j+1);

      /*
       * new step@20140704
       * (1)the different phases of memory utilization
       * (2)the avg. page accessing utilization
       * (3)memory pages layout and spectrum
       */
      num_access=0; //the total number of memory accesses across all pages
      for(j=0;j<30*100*100;j++)
          if(shadow1[j]>-1) //the page that is accessed at least once
              num_access+=(shadow1[j]+1);

      printk("the total number of memory accesses is %d, the average is %d\n",num_access, num_access/200);
      avg_page_utilization=num_access/all_pages;
      printk("Avg hot pages num is %d, all used pages num is %d, avg utilization of each page is %d\n", avg_hotpage, all_pages, avg_page_utilization);
      //yanghao:print the information about reuse-distance
      if ((times == 0) && (reuse_time[0] ==0))
          printk("the page No.%d is not available.",random_page);
      else
      {
          if ((times == 0) && (reuse_time[0] == 0))
              printk("the page No.%d was not used in this 200 loops.",random_page);
          else
          {
              if (times < 200)
                  times++;
              printk("the reusetime of page No.%d is:",random_page);
              for (j = 0; j < times; j++)
                  printk("%d ",reuse_time[j]);
              printk("\n");
              printk("the page No.%d is dirty at:",random_page);
              for (j = 0; j < 200; j++)
                  if (dirty_page[j] == 1)
                      printk("%d ",j);
          }
      }
      printk("\n");

      //yanghao:print the information about reading & writting.
      w2r = 0, r2w = 0, ri = 0, wi = 0;
      for (j=0; j<300000; j++)
      {
          if (read_times[j] > write_times[j] * 2)
          {
              if (out_data[j] == 2)
                  w2r++;
              out_data[j] = 1;
              history[loops][j] = 1;
              ri++;
              continue;
          }
          else
          {
              if (write_times[j] > 0)
              {
                  if (out_data[j] == 1)
                      r2w++;
                  history[loops][j] = 2;
                  out_data[j] = 2;
                  wi++;
              }
          }
      }
      loops++;
      printk("The number of reading dominant pages is: %d .\n",ri);
/*      gap = (i<200?1:(i/200));
      for (j=0;j<i;j+=gap)
          printk("%d ",out_data[j]);

      for (i=0, j=0; j<300000; j++)
          if (read_times[j] < write_times[j])
              out_data[i++] = j;
*/
      printk("The number of writing dominant pages is: %d .\n",wi);
/*
      gap = (i<200?1:(i/200));
      for (j=0;j<i;j+=gap)
          printk("%d ",out_data[j]);
*/
      printk("The number of pages(RD --> WD) is: %d \nThe number of pages(WD --> RD) is: %d \n",r2w,w2r);
      printk("\n\n");
      return 1;
}
#endif

module_init(timer_init);
module_exit(timer_exit);
MODULE_AUTHOR("leiliu");
MODULE_LICENSE("GPL");
