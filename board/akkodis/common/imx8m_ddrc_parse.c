#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <asm/arch/ddr.h>
#include "imx8m_ddrc_parse.h"

#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

static uint32_t letou32(const uint8_t* in)
{
	return	  ((uint32_t) in[0] << 0)
			| ((uint32_t) in[1] << 8)
			| ((uint32_t) in[2] << 16)
			| ((uint32_t) in[3] << 24);
}

static size_t buf_to_u32(uint32_t* value, const uint8_t* buf, size_t buf_len, size_t buf_pos)
{
	if (buf_len - buf_pos < sizeof(uint32_t))
		return 0;
	*value = letou32(buf + buf_pos);
	return sizeof(uint32_t);
}

static size_t buf_to_dram_cfg_param(struct dram_cfg_param** param, uint32_t param_size, const uint8_t* buf, size_t buf_len, size_t buf_pos)
{
	const size_t reg_size = MEMBER_SIZE(struct dram_cfg_param, reg);
	const size_t val_size = MEMBER_SIZE(struct dram_cfg_param, val);
	const size_t size = param_size * sizeof(struct dram_cfg_param);
	size_t pos = buf_pos;

	if (size > buf_len - pos)
		return 0;
	*param = malloc(size);
	if (*param == NULL)
		return 0;
	struct dram_cfg_param* ptr = *param;
	for (uint32_t i = 0; i < param_size; ++i) {
		ptr[i].reg = letou32(buf + pos);
		ptr[i].val = letou32(buf + pos + reg_size);
		pos += reg_size + val_size;
	}
	return pos - buf_pos;
}

static size_t buf_to_dram_fsp_msg(struct dram_fsp_msg** fsp, uint32_t fsp_size, const uint8_t* buf, size_t buf_len, size_t buf_pos)
{
	const size_t drate_size = MEMBER_SIZE(struct dram_fsp_msg, drate);
	const size_t fw_type_size = MEMBER_SIZE(struct dram_fsp_msg, fw_type);
	const size_t fsp_cfg_num_size = MEMBER_SIZE(struct dram_fsp_msg, fsp_cfg_num);
	const size_t size = fsp_size * sizeof(struct dram_fsp_msg);
	size_t pos = buf_pos;

	if (size > buf_len - pos)
		return 0;
	*fsp = malloc(size);
	if (*fsp == NULL)
		return 0;
	struct dram_fsp_msg* ptr = *fsp;
	for (uint32_t i = 0; i < fsp_size; ++i) {
		ptr[i].drate = letou32(buf + pos);
		ptr[i].fw_type = letou32(buf + pos + drate_size);
		ptr[i].fsp_cfg_num = letou32(buf + pos + drate_size + fw_type_size);
		pos += drate_size + fw_type_size + fsp_cfg_num_size;
	}
	return pos - buf_pos;
}

int parse_dram_timing_info(struct dram_timing_info* dram_timing_info, const uint8_t* buf, size_t len)
{
	struct dram_cfg_param* ddrc_cfg = NULL;
	uint32_t ddrc_cfg_num = 0;
	struct dram_cfg_param* ddrphy_cfg = NULL;
	uint32_t ddrphy_cfg_num = 0;
	struct dram_fsp_msg* fsp_msg = NULL;
	uint32_t fsp_msg_num = 0;
	struct dram_cfg_param* ddrphy_trained_csr = NULL;
	uint32_t ddrphy_trained_csr_num = 0;
	struct dram_cfg_param* ddrphy_pie = NULL;
	uint32_t ddrphy_pie_num = 0;

	size_t pos = 0;
	size_t r = 0;

	/* ddrc_cfg */
	r = buf_to_u32(&ddrc_cfg_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;
	r = buf_to_dram_cfg_param(&ddrc_cfg, ddrc_cfg_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;

	/* ddrphy_cfg */
	r = buf_to_u32(&ddrphy_cfg_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;
	r = buf_to_dram_cfg_param(&ddrphy_cfg, ddrphy_cfg_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;

	/* fsp_msg */
	r = buf_to_u32(&fsp_msg_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;
	r = buf_to_dram_fsp_msg(&fsp_msg, fsp_msg_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;
	for (uint32_t i = 0; i < fsp_msg_num; ++i) {
		r = buf_to_dram_cfg_param(&fsp_msg[i].fsp_cfg, fsp_msg[i].fsp_cfg_num, buf, len, pos);
		if (r == 0) {
			r = -EINVAL;
			goto exit;
		}
		pos += r;
	}

	/* ddrphy_trained_csr */
	r = buf_to_u32(&ddrphy_trained_csr_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;
	r = buf_to_dram_cfg_param(&ddrphy_trained_csr, ddrphy_trained_csr_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;

	/* ddrphy_pie */
	r = buf_to_u32(&ddrphy_pie_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;
	r = buf_to_dram_cfg_param(&ddrphy_pie, ddrphy_pie_num, buf, len, pos);
	if (r == 0) {
		r = -EINVAL;
		goto exit;
	}
	pos += r;

	/* fsp_table */
	const size_t fsp_table_size = MEMBER_SIZE(struct dram_timing_info, fsp_table);
	for (size_t i = 0; i < fsp_table_size / sizeof(uint32_t); ++i) {
		r = buf_to_u32(&dram_timing_info->fsp_table[i], buf, len, pos);
		if (r == 0) {
			r = -EINVAL;
			goto exit;
		}
		pos += r;
	}

	r = 0;
exit:
	if (r == 0) {
		dram_timing_info->ddrc_cfg = ddrc_cfg;
		dram_timing_info->ddrc_cfg_num = ddrc_cfg_num;
		dram_timing_info->ddrphy_cfg = ddrphy_cfg;
		dram_timing_info->ddrphy_cfg_num = ddrphy_cfg_num;
		dram_timing_info->fsp_msg = fsp_msg;
		dram_timing_info->fsp_msg_num = fsp_msg_num;
		dram_timing_info->ddrphy_trained_csr = ddrphy_trained_csr;
		dram_timing_info->ddrphy_trained_csr_num = ddrphy_trained_csr_num;
		dram_timing_info->ddrphy_pie = ddrphy_pie;
		dram_timing_info->ddrphy_pie_num = ddrphy_pie_num;
	}
	else {
		if (ddrc_cfg != NULL)
			free(ddrc_cfg);
		if (ddrphy_cfg != NULL)
			free(ddrphy_cfg);
		if (fsp_msg != NULL) {
			for (size_t i = 0; i < fsp_msg_num; ++i) {
				if (fsp_msg[i].fsp_cfg != NULL)
					free(fsp_msg[i].fsp_cfg);
			}
			free(fsp_msg);
		}
		if (ddrphy_trained_csr != NULL)
			free(ddrphy_trained_csr);
		if (ddrphy_pie != NULL)
			free(ddrphy_pie);
	}
	return r;
}
