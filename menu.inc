<?php

$menuEntries = Array(
	Array(
		'text' => 'Net2RF Auto Mode',
		'type' => 'content',
		'page' => 'net2rf.php',
		'wrap' => 1
	),
	Array(
		'text' => 'Net2RF Auto Mode - Home',
		'type' => 'help',
		'page' => 'https://github.com/mikeneiderhauser/fpp-net2rf',
		'wrap' => 1
	)
);

foreach ($menuEntries as $entry)
{
	if ($entry['type'] != $menu)
		continue;

	if (preg_match('/^http.?:\/\//', $entry['page']))
	{
		printf("<li><a href='%s' target='_blank'>%s</a></li>\n",
			$entry['page'], $entry['text']);
	}
	else
	{
		$nopage = '';
		if (isset($entry['wrap']) && ($entry['wrap'] == 0))
			$nopage = '&nopage=1';

		printf("<li><a href='plugin.php?plugin=%s&page=%s%s'>%s</a></li>\n",
			$plugin, $entry['page'], $nopage, $entry['text']);
	}
}
?>
