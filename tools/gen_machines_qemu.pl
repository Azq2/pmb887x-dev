#!/usr/bin/env perl
use warnings;
use strict;
use File::Basename;
use lib dirname(__FILE__).'/lib';
use Data::Dumper;
use List::Util qw(min max);
use Sie::CpuMetadata;
use Sie::BoardMetadata;
use Sie::Utils;

my $BOARD_DEF_TPL = qq|
static void pmb887x_{board}_init(MachineState *machine) {
	return pmb887x_init(machine, {board_def});
}

static void pmb887x_{board}_class_init(ObjectClass *oc, void *data) {
	MachineClass *mc = MACHINE_CLASS(oc);
	mc->desc = "{vendor} {model} ({cpu_name})";
	mc->init = pmb887x_{board}_init;
	mc->block_default_type = IF_PFLASH;
	mc->ignore_memory_transaction_failures = true;
	mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
	mc->default_ram_size = {ram} * 1024 * 1024;
}

static const TypeInfo pmb887x_{board}_type = {
	.name = MACHINE_TYPE_NAME("{machine_name}"),
	.parent = TYPE_MACHINE,
	.class_init = pmb887x_{board}_class_init
};

static void pmb887x_{board}_machine_init(void) {
	type_register_static(&pmb887x_{board}_type);
}

type_init(pmb887x_{board}_machine_init);

|;

my $str = qq|
#include "hw/arm/pmb887x/qemu-machines.h"
#include "hw/arm/pmb887x/regs.h"
|;

for my $board (@{Sie::BoardMetadata::getBoards()}) {
	my $board_meta = Sie::BoardMetadata->new($board);
	$str .= genBoardInfo($board_meta);
}

print $str."\n";

sub genBoardInfo {
	my ($board_meta) = @_;
	
	my $cpu_name = uc($board_meta->{cpu}->{name});
	my $board_def = "BOARD_".uc($board_meta->{name});
	my $machine_name = lc($board_meta->{vendor})."-".lc($board_meta->{name});
	my $machine_name_id = lc($board_meta->{vendor})."_".lc($board_meta->{name});
	
	my $str = $BOARD_DEF_TPL;
	$str =~ s/{board}/$machine_name_id/g;
	$str =~ s/{model}/$board_meta->{name}/g;
	$str =~ s/{vendor}/$board_meta->{vendor}/g;
	$str =~ s/{ram}/$board_meta->{ram}/g;
	$str =~ s/{machine_name}/$machine_name/g;
	$str =~ s/{cpu_name}/$cpu_name/g;
	$str =~ s/{board_def}/$board_def/g;
	
	return $str;
}
