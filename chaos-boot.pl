use warnings;
use strict;
use Device::SerialPort;
use Getopt::Long;
use File::Basename qw|dirname|;
use Time::HiRes qw|usleep time|;
use Linux::Termios2;
use POSIX qw( :termios_h );
use Data::Dumper;
no utf8;

$| = 1;

main();

sub main {
	my $device = "/dev/ttyUSB3";
	my $boot_speed = 115200;
	my $speed = 1600000;
	my $ign = 0;
	my $dtr = 0;
	my $rts = 0;
	my $bkey;
	my $exec_file;
	my $as_hex = 0;
	my $bootloader = dirname(__FILE__)."/bootloader/bootloader.bin";

	GetOptions (
		"device=s" => \$device, 
		"boot-speed=s" => \$boot_speed, 
		"speed=s" => \$speed, 
		"ign" => \$ign, 
		"dtr" => \$dtr, 
		"rts" => \$rts, 
		"bkey=s" => \$bkey, 
		"hex" => \$as_hex, 
		"exec=s" => \$exec_file
	);

	my $port = Device::SerialPort->new($device);
	die("open port error ($device)") if (!$port);

	$port->baudrate($boot_speed);
	
	$port->read_char_time(0);
	$port->read_const_time($ign ? 20 : 100);

	$port->dtr_active($dtr);
	$port->rts_active($rts);
	
	$SIG{INT} = $SIG{TERM} = sub {
		$port->dtr_active(0);
		$port->rts_active(0);
		exit(0);
	};
	
	$port->write_settings;
	if ($ign) {
		while (readb($port) != -1) { }
	}
	
	print "Please, short press red button!\n";
	
	my $last_dtr_val = 0;
	my $last_dtr = 0;
	my $last_dtr_timeout = 0.5;
	my $read_zero = 0;
	my $boot_ok = 0;
	while (1) {
		if ($ign) {
			if (time - $last_dtr > $last_dtr_timeout || $read_zero > 0) {
				$last_dtr_val = 0 if ($read_zero > 0); # поверофнулся типа
				
				$last_dtr_timeout = $last_dtr_val ? 1.5 : 0.5;
				$last_dtr_val = !$last_dtr_val;
				$last_dtr = time;
				$read_zero = 0;
				$port->dtr_active($last_dtr_val);
				
				print "^" if ($last_dtr_val);
			}
		}
		
		$port->write("AT");
		
		my $c = readb($port);
		if ($c == 0xB0 || $c == 0xC0) {
			$port->dtr_active($dtr) if ($ign);
			
			print "\n";
			print "SGOLD detected!\n" if ($c == 0xB0);
			print "NewSGOLD detected!\n" if ($c == 0xC0);
			
			$port->read_char_time(5000);
			$port->read_const_time(5000);
			
			print "Sending boot...\n";
			my $boot = mk_chaos_boot($bkey);
			write_boot($port, $boot);
			
			$c = readb($port);
			if ($c == 0xA5) {
				usleep(200 * 1000);
				
				# Странная проверка в PV Буте
				$port->write("\x55");
				$c = readb($port);
				
				if ($c != 0xAA) {
					print "Boot init error\n";
					exit(1);
				}
				
				chaos_ping($port) || exit(1);
				chaos_keep_alive($port);
				if ($speed != $boot_speed) {
					chaos_set_speed($port, $speed) or exit(1);
				}
				chaos_keep_alive($port);
				print "Chaos Bootloader - OK\n";
				
				# Запуск файла в RAM
				if ($exec_file) {
					my $addr = 0xA8000000;
					
					my $raw = "";
					open(F, "<$exec_file") or die("open($exec_file): $!");
					while (!eof(F)) {
						my $buff;
						read F, $buff, 2048;
						$raw .= $buff;
					}
					close(F);
					chaos_keep_alive($port);
					
					printf("Load $exec_file to RAM (%08X)... (size=%d)\n", $addr, length($raw));
					chaos_write_ram($port, $addr, $raw, 1024 * 3.5) or die("load error");
					
					printf("Exec %08X...\n", $addr);
					chaos_goto($port, $addr);
				}
			} else {
				printf("Invalid answer: %02X\n", $c);
				printf("Chaos bootloader not found!\n\n");
				next;
			}
			
			if ($as_hex) {
				while (($c = readb($port)) >= 0) {
					my $str = chr($c);
					printf("%s | %02X\n", ($str =~ /[[:print:]]/ ? "'".$str."'" : " ? "), $c);
				}
			} else {
				while (($c = readb($port)) >= 0) {
					print chr($c);
				}
			}
			last;
		} elsif ($c == 0) {
			++$read_zero;
		}
		print ".";
	}
}

sub chaos_ping {
	my ($port) = @_;
	$port->write("A");
	my $c = readb($port);
	if ($c != 0x52) {
		warn sprintf("[chaos_ping] Invalid answer 0x%02X\n", $c);
		return 0;
	}
	return 1;
}

sub chaos_keep_alive {
	my ($port) = @_;
	$port->write(".");
	return 1;
}

sub chaos_set_speed {
	my ($port, $speed) = @_;
	
	my %CHAOS_SPEEDS = (
		57600 => 0x00, 
		115200 => 0x01, 
		230400 => 0x02, 
		460800 => 0x03, 
		614400 => 0x04, 
		921600 => 0x05, 
		1228800 => 0x06, 
		1600000 => 0x07, 
		1500000 => 0x08
	);
	
	if (!exists($CHAOS_SPEEDS{$speed})) {
		warn("Invalid speed $speed! Allowed: ".split(", ", keys(%CHAOS_SPEEDS)));
		return 0;
	}
	
	my $old_speed = $port->baudrate;
	$port->write("H".chr($CHAOS_SPEEDS{$speed}));
	my $c = readb($port);
	if ($c == 0x68) {
		set_port_baudrate($port, $speed);
		$port->write("A");
		$c = readb($port);
		if ($c == 0x48) {
			# Успешно установили скорость
			return 1;
		}
	}
	set_port_baudrate($port, $old_speed);
	warn sprintf("[chaos_set_speed] Invalid answer 0x%02X", $c);
	return 0;
}

sub chaos_goto {
	my ($port, $addr) = @_;
	
	my $test = 0;
	$port->write("G");
	$port->write(chr(($addr >> 24) & 0xFF).chr(($addr >> 16) & 0xFF).chr(($addr >> 8) & 0xFF).chr($addr & 0xFF));
	$port->write(chr(($addr >> 24) & 0xFF).chr(($addr >> 16) & 0xFF).chr(($addr >> 8) & 0xFF).chr($addr & 0xFF));
	$port->write(chr(($addr >> 24) & 0xFF).chr(($addr >> 16) & 0xFF).chr(($addr >> 8) & 0xFF).chr($addr & 0xFF));
	
	return 1;
}

sub chaos_write_ram {
	my ($port, $dst_addr, $buff, $chunk) = @_;
	
	my @blocks = ();
	my $buff_size = length($buff);
	for (my $j = 0; $j < $buff_size; $j += $chunk) {
		my $tmp = substr($buff, $j, $chunk);
		
		my $chk;
		my $size = length($tmp);
		for (my $i = 0; $i < $size; ++$i) {
			$chk ^= ord(substr($tmp, $i, 1));
		}
		my $addr = $dst_addr + $j;
		
		push @blocks, [
			$addr, $size, 
			"W".
			chr(($addr >> 24) & 0xFF).chr(($addr >> 16) & 0xFF).chr(($addr >> 8) & 0xFF).chr($addr & 0xFF).
			chr(($size >> 24) & 0xFF).chr(($size >> 16) & 0xFF).chr(($size >> 8) & 0xFF).chr($size & 0xFF).
			$tmp.chr($chk)
		];
	}
	
	my $i = 0;
	my $start = time;
	for my $block (@blocks) {
		my $addr = $block->[0];
		my $size = $block->[1];
		
		if ($i % 10 == 0) {
			printf("                                                    \r");
			printf("#$i %02d%s [WRITE] %08X-%08X (%.02f Kbps)\r", int(($addr - $dst_addr) / $buff_size * 100), "%", $addr, $addr + $size, (($addr - $dst_addr) / 1024) / (time - $start));
		}
		
		my $tries = 10;
		while (1) {
			$port->write($block->[2]);
		
			my $ok = $port->read(2);
			if ($ok ne "OK") {
				warn sprintf("\n[chaos_write_ram] Invalid answer '%02X%02X'", ord(substr($ok, 0, 1)), ord(substr($ok, 1, 1)));
				if ($tries--) {
					chaos_ping($port) || exit(1);
					next;
				}
				exit(1);
			}
			last;
		}
		++$i;
	}
	print "\n";
	return 1;
}

sub mk_chaos_boot {
	my $bkey = shift;
	# Тут не совсем бут Chaos'a. Тут бут PV. Но он, как я понял, модификация бута от Chaos
	my $data = 
		"08D04FE200000FE1C00080E300F021E10000A0E3A80100EB541B9FE57C0091E50100C0E3090000EA5349454D454E535F424F4F54434F44450300000000C20100".
		($bkey || "00000000000000000000000000000000").
		"7C0081E50800A0E3280081E50100A0E3950100EB410200EBA500A0E3720100EB7D0100EB550050E30200000A2E0050E35302000BF9FFFFEA980100EBAA00A0E3690100EB740100EB410050E35200A003FAFFFF0A480050E36200000A490050E36E00000A510050E32B00000A540050E32C00000A520050E34B00000A460050E3D300000A450050E37D00000A500050E36A00000A570050E30D00000A470050E31000000A4F0050E33300000A430050E33600000A580050E36B02000A457F4FE2560050E38E02000A2E0050E32C02000BDBFFFFEA3E0100EB0640A0E10750A0E12B0100EB140100EA390100EB070056E10000000BD0FFFFEA0600A0E13A0100EB2604A0E1380100EB2608A0E1360100EB260CA0E1340100EB06F0A0E11E0200EB1D0200EBFEFFFFEA290100EB040096E4041096E4010000E0010070E30000A0130100001A087057E2F7FFFF1ABBFFFFEA0E40A0E11E0100EB940000EB400E57E39700002A0610A0E1015A8FE2195D85E2A780A0E189005AE314FF2FE1F3FFFFEBC801001B89005AE3BB01000B030000EAEEFFFFEBC101001B89005AE3B401000B056B8FE2826F86E2000000EA080100EB0080A0E30100D6E4008028E00A0100EB017057E2FAFFFF1A4F00A0E3060100EB4B00A0E3040100EB0800A0E1020100EB0000A0E395FFFFEA0B0100EB0060A0E16800A0E3FC0000EBA00EA0E3010050E2FDFFFF1A0600A0E1CF0000EB020100EB410050E3FCFFFF1A4800A0E387FFFFEA066B8FE2E26F86E28070A0E30100D6E4ED0000EB017057E2FBFFFF1A80FFFFEAE30000EB590000EBA844A0E30710A0E10650A0E1040095E4040084E4041051E2FBFFFF1A5500A0E3DF0000EBF40000EB0080A0E1F20000EB0050A0E1A84488E2570000EAEE0000EB0060A0E1470000EB010000EB0D0000EB69FFFFEA0E90A0E189005AE31B00001A6000A0E3B000C6E1D000A0E3B000C6E10000A0E12000A0E3B000C6E1D000A0E3B000C6E119FF2FE10E90A0E10100A0E3C30000EB0100A0E3C10000EB89005AE31000001AA40100EB7000A0E3B000C6E1B000D6E1800010E3F9FFFF0A3A0010E35C00001AFF00A0E3B000C6E1140000EA0610A0E18000A0E3700100EB3000A0E36E0100EB19FF2FE1401BA0E3011041E2A02BA0E3012052E21500000AB000D6E1010050E1FAFFFF1AB000D6E1010050E1F7FFFF1AB000D6E1010050E1F4FFFF1A040000EB0200A0E39D0000EB0200A0E39B0000EB19FF2FE19000A0E3B000C6E10000A0E3B000C6E1F000A0E3B000C6E11EFF2FE1F7FFFFEB4500A0E3900000EB4500A0E323FFFFEAA00456E30200003AA80456E30000002A1EFF2FE1FF00A0E3870000EBFF00A0E31AFFFFEA7E0000EBF4FFFFEBA844A0E30750A0E1ACFFFFEB690000EBB7FFFFEBA884A0E389005AE32500001A064B8FE26B4F84E25500D4E50130A0E31330A0E1A330A0E10640A0E1A750A0E1580100EBE800A0E3B000C4E1B000D4E1800010E31200000A010043E2B000C4E10310A0E1B200D8E0B200C4E0011051E2FBFFFF1AD000A0E3B000C6E1B000D6E1800010E3FCFFFF0A3A0010E30400001A035055E0E9FFFF8AFF00A0E3B000C6E11F0000EA5000A0E3B000C6E1FF00A0E3B000C6E1CBFFFFEA0610A0E12000A0E3110100EB0640A0E1A750A0E1350100EB802BA0E3A000A0E3B000C4E1B200D8E0B000C4E1012052E2B4FFFF0AB010D4E1000051E1FAFFFF1AB010D4E1000051E1F7FFFF1AB010D4E1000051E1F4FFFF1A012082E2024084E2015055E2ECFFFF1A9FFFFFEB0300A0E3380000EB0300A0E3360000EB0080A0E30610A0E1A720A0E1B200D1E0008088E0012052E2FBFFFF1A0800A0E12D0000EB2804A0E12B0000EB4F00A0E3290000EB4B00A0E3BCFEFFEA1C108FE2001191E7F124A0E32108A0E1140082E50108A0E12008A0E1180082E51EFF2FE1D8011900D8010C00B401050092000000C3000000270100008A01000000000000D00100000E90A0E10080A0E3F80000EB1D0000EB008028E00100C4E4015055E2F9FFFF1A180000EB080050E10000001A19FF2FE1BB00A0E3070000EBBB00A0E39AFEFFEA0E90A0E1190000EB0060A0E1170000EB0070A0E119FF2FE1F124A0E3201092E5FF10C1E3011080E1201082E5681092E5021011E2FCFFFF0A701092E5021081E3701082E5D90000EAF114A0E3680091E5040010E2FBFFFF0A700091E5040080E3700081E5240091E5FF0000E21EFF2FE10E50A0E1F3FFFFEB004CA0E1F1FFFFEB004884E1EFFFFFEB004484E1EDFFFFEB000084E115FF2FE1B0349FE5241093E50E10C1E3F01081E3282093E50C2002E2021081E1241083E50D10C1E3021081E3010080E1240083E51EFF2FE10E70A0E1C50000EB014A8FE2074C84E20410A0E18020A0E30000A0E3040081E4042052E2FCFFFF1A0410A0E154049FE52020A0E37E0000EB4C049FE51020A0E37B0000EB44049FE51020A0E3780000EBA004A0E3400084E5A014A0E39000A0E3780000EBB0A0D1E1B0A5C4E17E0000EBB000D1E1B025D4E1020050E0B005C401B205C4012800000A9000A0E36D0000EBB200D1E1B205C4E1730000EB01005AE30F00000A04005AE30D00000A89005AE31D00001A445084E2406F81E2026086E20680A0E3440000EB745084E20280A0E3410000EB026086E20480A0E33E0000EB110000EA445084E20160A0E10680A0E3440000EB745084E20280A0E3410000EB806081E2785084E20480A0E33D0000EB785084E2000094E5010090E20200001A406F81E20480A0E3360000EB9800A0E3BA0AC1E10000A0E1542084E2BE04D1E10100C2E4543081E2B200D3E00100C2E4FF0003E27A0050E3FAFFFF1A420000EB5400D4E51A0050E31800000A7D0000EBFF1402E29800A0E3BA0AC1E10000A0E1B002D1E1510050E31000001A5400D4E5010080E25400C4E55720D4E5022184E0582082E25A3081E2B200D3E00100C2E4FF0003E27A0050E3FAFFFF1AB805D1E15720D4E5002082E05720C4E5260000EB17FF2FE19800A0E3000000EA9000A0E30E90A0E1170000EBB200D6E0B200C5E0018058E2FBFFFF1A1B0000EB19FF2FE19800A0E3000000EA8800A0E30E90A0E10C0000EBB200D6E0B200C5E0018058E2FBFFFF1A9000A0E3060000EBB010C1E119FF2FE10130D0E40130C1E4012052E2FBFFFF1A1EFF2FE1AA30A0E3A02E81E2BA3AC2E15530A0E3502E81E2B435C2E1A02E81E2BA0AC2E11EFF2FE1F000A0E389005AE3FF00A003B000C1E11EFF2FE1FC019FE5600090E52004A0E1FF0000E2FC119FE5CC2041E2140050E30100001A601081E2042082E2043082E20C4043E201C0A0E10100A0E3000082E51000A0E3000083E5500EA0E3000081E5400CA0E3510E80E3000084E5B8119FE500B091E5050000EAAC219FE5000092E50B1040E0800F51E30800003A00B0A0E100009CE5000BA0E100209CE5A00FE0E1802FC2E3010000E2800482E100008CE51EFF2FE1F014A0E370019FE5880081E56C019FE5C80081E50600A0E3400081E560019FE5500081E52300A0E3600081E554019FE5800081E5A00081E54C019FE5C00081E5E00081E51EFF2FE1F014A0E3A024A0E3212082E2190050E310208212802081E5A02081E540278202402782E2902081E5B02081E510019FE5D00081E5F00081E51EFF2FE1A874A0E3030B8FE2160E80E2041090E4042090E4043090E4044090E4045090E4406BA0E31EFF2FE1F4FFFFEB041087E4042087E4800817E30300001A043087E4044087E4045087E4F7FFFFEABBFFFFEB5500A0E3D3FEFFEBE8FFFFEB040097E4010050E11300001A040097E4020050E11000001A800817E35EFDFF1A016056E20300001AADFFFFEB5600A0E3C5FEFFEBDBFFFFEB040097E4030050E10500001A040097E4040050E10200001A040097E4050050E1E8FFFF0A4500A0E3B9FEFFEB0700A0E1B7FEFFEB2704A0E1B5FEFFEB2708A0E1B3FEFFEB270CA0E146FDFFEA000040F400E003A010E403A000E403A0180130F42000B0F4410000A800027230701C8900110000A000265200";
	return hex2bin($data);
}

sub hex2bin {
	my $hex = shift;
	$hex =~ s/\s+//gim;
	$hex = "0$hex" if (length($hex) % 2 != 0);
	$hex =~ s/([A-F0-9]{2})/chr(hex($1))/ge;
	return $hex;
}

sub write_boot {
	my ($port, $boot) = @_;
	
	# Считаем XOR
	my $chk = 0;
	my $len = length($boot);
	for (my $i = 0; $i < $len; ++$i) {
		$chk ^= ord(substr($boot, $i, 1));
	}
	
	$port->write("\x30");
	
	# Шлём размер бута
	$port->write(chr($len & 0xFF).chr(($len >> 8) & 0xFF));
	
	# Шлём бут
	$port->write($boot);
	
	# Шлём XOR бута
	$port->write(chr($chk));
	
	my $c = readb($port);
	return 1 if ($c == 0xC1);
	
	warn sprintf("Invalid answer: %02X\n", $c);
	return 0;
}

sub set_port_baudrate {
	my ($port, $baudrate) = @_;
	my $termios = Linux::Termios2->new;
	$termios->getattr($port->FILENO);
	$termios->setospeed($baudrate);
	$termios->setispeed($baudrate);
	$termios->setattr($port->FILENO, TCSANOW);
	return -1;
}

sub readb {
	my ($port) = @_;
	my ($count, $char) = $port->read(1);
	return ord($char) if ($count);
	return -1;
}