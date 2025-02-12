/* Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <rdma/fi_errno.h>

#include "shared.h"
#include "benchmarks/benchmark_shared.h"
#include "efa_exhaust_mr_reg_common.h"

static int run(void)
{
	int i, ret = 0;

	if (!(opts.options & FT_OPT_SIZE)) {
		for (i = 0; i < TEST_CNT; i++) {
			if (!ft_use_size(i, opts.sizes_enabled))
				continue;
			opts.transfer_size = test_size[i].size;
			init_test(&opts, test_name, sizeof(test_name));
			ret = pingpong();
			if (ret)
				return ret;
		}
	} else {
		init_test(&opts, test_name, sizeof(test_name));
		ret = pingpong();
		if (ret)
			return ret;
	}

	return ft_finalize();
}

int main(int argc, char **argv)
{
	int op, ret, err;
	size_t registered;
	void *buffers[EFA_MR_REG_LIMIT];
	struct ibv_mr *mr_reg_vec[EFA_MR_REG_LIMIT];
	struct ibv_pd *pd;

	opts = INIT_OPTS;
	opts.options |= FT_OPT_SKIP_REG_MR;
	opts.mr_mode &= ~FI_MR_LOCAL;

	hints = fi_allocinfo();
	if (!hints)
		return EXIT_FAILURE;

	while ((op = getopt_long(argc, argv, "Uh" CS_OPTS INFO_OPTS BENCHMARK_OPTS,
				 long_opts, &lopt_idx)) != -1) {
		switch (op) {
		default:
			if (!ft_parse_long_opts(op, optarg))
				continue;
			ft_parse_benchmark_opts(op, optarg);
			ft_parseinfo(op, optarg, hints, &opts);
			ft_parsecsopts(op, optarg, &opts);
			break;
		case 'U':
			hints->tx_attr->op_flags |= FI_DELIVERY_COMPLETE;
			break;
		case '?':
		case 'h':
			ft_csusage(argv[0], "Ping pong client and server using RDM after exhausting MR limits on the EFA device.");
			ft_benchmark_usage();
			ft_longopts_usage();
			return EXIT_FAILURE;
		}
	}

	if (optind < argc)
		opts.dst_addr = argv[optind];

	hints->ep_attr->type = FI_EP_RDM;
	hints->caps = FI_MSG;
	hints->mode |= FI_CONTEXT;
	hints->domain_attr->mr_mode = opts.mr_mode;
	hints->domain_attr->threading = FI_THREAD_DOMAIN;
	hints->addr_format = opts.address_format;

	ret = ft_init_fabric();
	if (ret)
		return ft_exit_code(ret);

	/* Run progress engine to grow bounce buffers before exhausting MRs */
	ft_force_progress();

	ft_sync();
	if (opts.dst_addr) {
		err = ft_efa_alloc_bufs(buffers, EFA_MR_REG_BUF_SIZE,
					EFA_MR_REG_LIMIT);
		if (err)
			FT_PRINTERR("alloc bufs", -err);

		err = ft_efa_setup_ibv_pd(&pd);
		if (err)
			FT_PRINTERR("ibv protection domain", -err);

		printf("Exhausting MRs on client\n");
		err = ft_efa_register_mr_reg(pd, buffers, EFA_MR_REG_BUF_SIZE,
					     mr_reg_vec, EFA_MR_REG_LIMIT,
					     &registered);
		if (err)
			FT_PRINTERR("ibv mr reg", -err);
	}

	ft_sync();
	printf("Running pingpong test\n");
	ret = run();

	if (opts.dst_addr) {
		printf("Deregistering MRs on client\n");
		err = ft_efa_deregister_mr_reg(mr_reg_vec, registered);
		if (err)
			FT_PRINTERR("ibv mr dereg", -err);
		ft_efa_destroy_ibv_pd(pd);
	}

	ft_free_res();

	return ft_exit_code(ret);
}
