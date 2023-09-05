#!/usr/bin/env perl
use warnings;
use strict;
use File::Basename;
use lib dirname(__FILE__).'/lib';
use Data::Dumper;
use File::Slurp qw(read_file);
use Sie::CFI;
use Sie::Utils;

my $path = getDataDir().'/cfi';
opendir my $fp, $path or die "opendir($path): $!";
my @files = readdir $fp;
closedir $fp;

print qq|/* Autogenerated by $0 */\n#include "hw/arm/pmb887x/flash.h"\n\n|;

my @flashes;
for my $dump_file (@files) {
	next if $dump_file !~ /^([a-f0-9]+)-([a-f0-9]+)\.hex$/i;
	my ($vid, $pid) = (hex $1, hex $2);
	
	my $dump_text = scalar(read_file("$path/$dump_file"));
	$dump_text =~ s/#.*?$//gm;
	$dump_text =~ s/^\s+|\s+$//g;
	
	my @dump = map { hex $_ } split(/\s+/s, $dump_text);
	my $info = Sie::CFI::parseFlashDump($vid, $pid, \@dump);
	
	my $var_name = sprintf("flash_%04x_%04x", $info->{vid}, $info->{pid});
	
	my $has_otp = @{$info->{pri}->{otp}} > 0;
	
	my @partitions;
	my $flash_offset = 0;
	
	for my $region (@{$info->{pri}->{regions}}) {
		my @erase_regions;
		
		my $part_size = 0;
		for my $erase (@{$region->{erase_regions}}) {
			 push @erase_regions, sprintf("{ 0x%X, 0x%X, 0x%X }", $part_size, $erase->{blocks} * $erase->{block_size}, $erase->{block_size});
			 $part_size += $erase->{blocks} * $erase->{block_size};
		}
		
		for (my $i = 0; $i < $region->{identical_banks}; $i++) {
			push @partitions, sprintf("{ 0x%08X, 0x%08X, { %s }, %d }", $flash_offset, $part_size, join(", ", @erase_regions), scalar(@erase_regions));
			$flash_offset += $part_size;
		}
	}
	
	# Hardware partitions
	print "const struct pmb887x_flash_part_t ${var_name}_parts[] = {\n";
	print "\t".join(",\n\t", @partitions)."\n";
	print "};\n";
	
	# CFI dump
	print "const struct uint8_t ${var_name}_cfi[] = {\n";
	print dumpHex("\t", $info->{cfi_bin});
	print "};\n";
	
	# PRI dump
	print "const struct uint8_t ${var_name}_pri[] = {\n";
	print dumpHex("\t", $info->{pri_bin});
	print "};\n";
	
	my @flash_struct = (
		vid			=> sprintf("0x%04X", $info->{vid}),
		pid			=> sprintf("0x%04X", $info->{pid}),
		lock		=> sprintf("0x%04X", $info->{lock}),
		cr			=> sprintf("0x%04X", $info->{cr}),
		ehcr		=> sprintf("0x%04X", $info->{ehcr}),
		size		=> sprintf("0x%04X", $info->{cfi}->{flash_size}),
		cfi			=> "${var_name}_cfi",
		cfi_size	=> "ARRAY_SIZE(${var_name}_cfi)",
		pri			=> "${var_name}_pri",
		pri_size	=> "ARRAY_SIZE(${var_name}_pri)",
		parts		=> "${var_name}_parts",
		parts_count	=> "ARRAY_SIZE(${var_name}_parts)",
		pri_addr	=> sprintf("0x%X", $info->{cfi}->{pri_addr}),
		otp0_addr	=> sprintf("0x%X", $has_otp ? $info->{pri}->{otp}->[0]->{addr} : 0),
		otp0_size	=> sprintf("0x%X", $has_otp ? $info->{pri}->{otp}->[0]->{size} : 0),
		otp1_addr	=> sprintf("0x%X", $has_otp ? $info->{pri}->{otp}->[1]->{addr} : 0),
		otp1_size	=> sprintf("0x%X", $has_otp ? $info->{pri}->{otp}->[1]->{size} : 0),
	);
	
	my @flash_table;
	for (my $i = 0; $i < scalar(@flash_struct); $i += 2) {
		push @flash_table, [".".$flash_struct[$i], "= ".$flash_struct[$i + 1].","];
	}
	
	push @flashes, "\t{\n".printTable(\@flash_table, "\t\t")."\n\t},";
}
print "const pmb887x_flash_info_t flashes[] = {\n";
print join("\n", @flashes)."\n";
print "};\n";

print qq|
const pmb887x_flash_info_t *pmb887x_flash_find(uint16_t vid, uint16_t pid) {
	for (size_t i = 0; i < ARRAY_SIZE(flahes); i++) {
		if (flashes[i].vid == vid && flashes[i].pid == pid)
			return &flashes[i];
	}
	return NULL;
}
|;

sub dumpHex {
	my ($tab, $bin) = @_;
	my @lines;
	my @line;
	for (my $i = 0; $i < length($bin); $i++) {
		push @line, sprintf("0x%02X", ord(substr($bin, $i, 1)));
		if (scalar(@line) >= 16) {
			push @lines, [@line];
			@line = ();
		}
	}
	push @lines, [@line] if scalar(@line) > 0;
	return $tab.join("\n$tab", map { join(", ", @$_)."," } @lines)."\n";
}
