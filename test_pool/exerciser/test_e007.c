/** @file
 * Copyright (c) 2018, Arm Limited or its affiliates. All rights reserved.
 * SPDX-License-Identifier : Apache-2.0

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 **/
#include "val/include/sbsa_avs_val.h"
#include "val/include/val_interface.h"
#include "val/include/sbsa_avs_memory.h"
#include "val/include/sbsa_avs_exerciser.h"

#include "val/include/sbsa_avs_pcie.h"
#include "val/include/sbsa_avs_pcie_enumeration.h"

#define TEST_NUM   (AVS_EXERCISER_TEST_NUM_BASE + 7)
#define TEST_DESC  "Check PCI Express I/O Coherency   "

#define TEST_DATA_BLK_SIZE  512
#define TEST_DATA 0xDE

#define MEM_ATTR_CACHEABLE_SHAREABLE 0
#define MEM_ATTR_NON_CACHEABLE 1

void init_source_buf_data(void *buf, uint32_t size)
{

  uint32_t index;

  for (index = 0; index < size; index++) {
    *((char8_t *)buf + index) = TEST_DATA;
  }

}

static
void
payload (void)
{

  uint32_t pe_index;
  uint32_t instance;
  uint32_t dma_len;
  uint32_t e_bdf;
  uint32_t start_segment;
  uint32_t start_bus;
  uint32_t start_bdf;
  void *e_dev;
  void *src_buf_virt;
  void *src_buf_phys;
  void *dest_buf_virt;
  void *dest_buf_phys;

  e_dev = NULL;
  src_buf_virt = NULL;
  src_buf_phys = NULL;
  pe_index = val_pe_get_index_mpid (val_pe_get_mpid());

  /* Read the number of excerciser cards */
  instance = val_exerciser_get_info(EXERCISER_NUM_CARDS, 0);

  /* Set start_bdf segment and bus numbers to 1st ecam region values */
  start_segment = val_pcie_get_info(PCIE_INFO_SEGMENT, 0);
  start_bus = val_pcie_get_info(PCIE_INFO_START_BUS, 0);
  start_bdf = PCIE_CREATE_BDF(start_segment, start_bus, 0, 0);

  while (instance-- != 0) {

    /* Get the exerciser BDF */
    e_bdf = val_pcie_get_bdf(EXERCISER_CLASSCODE, start_bdf);
    start_bdf = val_pcie_increment_bdf(e_bdf);

    /* Derive exerciser device structure from its bdf */
    e_dev = val_pci_bdf_to_dev(e_bdf);

    /* Get a non-caheable DDR Buffer of size TEST_DATA_BLK_SIZE */
    src_buf_virt = val_memory_alloc_coherent(e_dev, TEST_DATA_BLK_SIZE, src_buf_phys);
    if (!src_buf_virt) {
      val_print(AVS_PRINT_ERR, "\n      Non-cacheable mem alloc failure %x", 02);
      val_set_status(pe_index, RESULT_FAIL(g_sbsa_level, TEST_NUM, 02));
      return;
    }

    /* Program exerciser to start sending TLPs  with No Snoop attribute header.
     * This includes setting Enable No snoop bit in exerciser control register.
     */
    if(val_exerciser_ops(NO_SNOOP_TLP_START, 0, instance)) {
      val_print(AVS_PRINT_ERR, "\n       Exerciser %x No Snoop enable error", instance);
      goto test_fail;
    }

    /* Set VA and PA addresses for the destination buffer and DMA size */
    dest_buf_virt = src_buf_virt + (TEST_DATA_BLK_SIZE / 2);
    dest_buf_phys = src_buf_phys + (TEST_DATA_BLK_SIZE / 2);
    dma_len = TEST_DATA_BLK_SIZE / 2;

    /* Initialize source buffer with test specific data */
    init_source_buf_data(src_buf_virt, dma_len);

    /* Program Exerciser DMA controller with the source buffer information */
    val_exerciser_set_param(DMA_ATTRIBUTES, (uint64_t)src_buf_phys, dma_len, instance);
    if (val_exerciser_ops(START_DMA, EDMA_TO_DEVICE, instance)) {
      val_print(AVS_PRINT_ERR, "\n      DMA write failure to exerciser %4x", instance);
      goto test_fail;
    }

    /* READ Back from Exerciser to validate above DMA write */
    val_exerciser_set_param(DMA_ATTRIBUTES, (uint64_t)dest_buf_phys, dma_len, instance);
    if (val_exerciser_ops(START_DMA, EDMA_FROM_DEVICE, instance)) {
      val_print(AVS_PRINT_ERR, "\n      DMA read failure from exerciser %4x", instance);
      goto test_fail;
    }

    if (memcmp(src_buf_virt, dest_buf_virt, dma_len)) {
      val_print(AVS_PRINT_ERR, "\n        I/O coherency failure for Exerciser %4x", instance);
      goto test_fail;
    }

    /* Stop exerciser sending TLPs with No Snoop attribute header */
    if (val_exerciser_ops(NO_SNOOP_TLP_STOP, 0, instance)) {
        val_print(AVS_PRINT_ERR, "\n       Exerciser %x No snoop TLP disable error", instance);
        goto test_fail;
    }

  }

  val_set_status(pe_index, RESULT_PASS(g_sbsa_level, TEST_NUM, 0));
  val_memory_free_coherent(e_dev, TEST_DATA_BLK_SIZE, src_buf_virt, src_buf_phys);
  return;

test_fail:
  val_set_status(pe_index, RESULT_FAIL(g_sbsa_level, TEST_NUM, 02));
  val_memory_free_coherent(e_dev, TEST_DATA_BLK_SIZE, src_buf_virt, src_buf_phys);
  return;
}

uint32_t
e007_entry (void)
{
  uint32_t num_pe = 1;
  uint32_t status = AVS_STATUS_FAIL;

  status = val_initialize_test (TEST_NUM, TEST_DESC, num_pe, g_sbsa_level);
  if (status != AVS_STATUS_SKIP) {
      val_run_test_payload (TEST_NUM, num_pe, payload, 0);
  }

  /* Get the result from all PE and check for failure */
  status = val_check_for_error (TEST_NUM, num_pe);

  val_report_status (0, SBSA_AVS_END (g_sbsa_level, TEST_NUM));

  return status;
}
