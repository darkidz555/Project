// SPDX-License-Identifier: GPL-2.0+
//
// Security related flags and so on.
//
// Copyright 2018, Michael Ellerman, IBM Corporation.

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/seq_buf.h>

#include <asm/security_features.h>


unsigned long powerpc_security_features __read_mostly = SEC_FTR_DEFAULT;

bool barrier_nospec_enabled;
static bool no_nospec;

static void enable_barrier_nospec(bool enable)
{
	barrier_nospec_enabled = enable;
	do_barrier_nospec_fixups(enable);
}

void setup_barrier_nospec(void)
{
	bool enable;

	/*
	 * It would make sense to check SEC_FTR_SPEC_BAR_ORI31 below as well.
	 * But there's a good reason not to. The two flags we check below are
	 * both are enabled by default in the kernel, so if the hcall is not
	 * functional they will be enabled.
	 * On a system where the host firmware has been updated (so the ori
	 * functions as a barrier), but on which the hypervisor (KVM/Qemu) has
	 * not been updated, we would like to enable the barrier. Dropping the
	 * check for SEC_FTR_SPEC_BAR_ORI31 achieves that. The only downside is
	 * we potentially enable the barrier on systems where the host firmware
	 * is not updated, but that's harmless as it's a no-op.
	 */
	enable = security_ftr_enabled(SEC_FTR_FAVOUR_SECURITY) &&
		 security_ftr_enabled(SEC_FTR_BNDS_CHK_SPEC_BAR);

	if (!no_nospec)
		enable_barrier_nospec(enable);
}

static int __init handle_nospectre_v1(char *p)
{
	no_nospec = true;

	return 0;
}
early_param("nospectre_v1", handle_nospectre_v1);

#ifdef CONFIG_DEBUG_FS
static int barrier_nospec_set(void *data, u64 val)
{
	switch (val) {
	case 0:
	case 1:
		break;
	default:
		return -EINVAL;
	}

	if (!!val == !!barrier_nospec_enabled)
		return 0;

	enable_barrier_nospec(!!val);

	return 0;
}

static int barrier_nospec_get(void *data, u64 *val)
{
	*val = barrier_nospec_enabled ? 1 : 0;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fops_barrier_nospec,
			barrier_nospec_get, barrier_nospec_set, "%llu\n");

static __init int barrier_nospec_debugfs_init(void)
{
	debugfs_create_file("barrier_nospec", 0600, powerpc_debugfs_root, NULL,
			    &fops_barrier_nospec);
	return 0;
}
device_initcall(barrier_nospec_debugfs_init);
#endif /* CONFIG_DEBUG_FS */

ssize_t cpu_show_meltdown(struct device *dev, struct device_attribute *attr, char *buf)
{
	bool thread_priv;

	thread_priv = security_ftr_enabled(SEC_FTR_L1D_THREAD_PRIV);

	if (rfi_flush || thread_priv) {
		struct seq_buf s;
		seq_buf_init(&s, buf, PAGE_SIZE - 1);

		seq_buf_printf(&s, "Mitigation: ");

		if (rfi_flush)
			seq_buf_printf(&s, "RFI Flush");

		if (rfi_flush && thread_priv)
			seq_buf_printf(&s, ", ");

		if (thread_priv)
			seq_buf_printf(&s, "L1D private per thread");

		seq_buf_printf(&s, "\n");

		return s.len;
	}

	if (!security_ftr_enabled(SEC_FTR_L1D_FLUSH_HV) &&
	    !security_ftr_enabled(SEC_FTR_L1D_FLUSH_PR))
		return sprintf(buf, "Not affected\n");

	return sprintf(buf, "Vulnerable\n");
}

ssize_t cpu_show_spectre_v1(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!security_ftr_enabled(SEC_FTR_BNDS_CHK_SPEC_BAR))
		return sprintf(buf, "Not affected\n");

	if (barrier_nospec_enabled)
		return sprintf(buf, "Mitigation: __user pointer sanitization\n");

	return sprintf(buf, "Vulnerable\n");
}
