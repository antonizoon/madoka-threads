This is part of the Bibliotheca Anonoma's initiative to save all the Madoka threads from /a/ into a Madoka Crate (#5) for eventual archival in the Internet Archive.

For more information, [see this page](http://wiki.puella-magi.net/Madoka_Crate).

## Helpful scripts

This bash script (txt2dir.sh) turns a list of files into folders. It comes from the Arch Linux forums.

    #!/bin/bash
    IFS='
    '
    for _dir in $(cat "$1"); do
      mkdir "$_dir"
    done

The following command takes in a list of files, places a number in ascending order in front, and adds a dash after the number.

    sed = Filelist.txt | sed 'N;s/\n/-/' > sed.txt && ./txt2dir.sh sed.txt && rm sed.txt