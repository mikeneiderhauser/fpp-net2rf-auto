<?php
$menuEntries = Array(
    Array('text' => 'Net2RF Bracelet Follow', 'type' => 'content', 'page' => 'net2rf.php', 'wrap' => 1),
);

foreach ($menuEntries as $entry) {
    if ($entry['type'] != $menu)
        continue;
    $nopage = (isset($entry['wrap']) && $entry['wrap'] == 0) ? '&nopage=1' : '';
    printf("<li><a href='plugin.php?plugin=%s&page=%s%s'>%s</a></li>\n", $plugin, $entry['page'], $nopage, $entry['text']);
}
?>
