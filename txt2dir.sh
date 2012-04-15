#!/bin/bash
IFS='
'
for _dir in $(cat "$1"); do
  mkdir "$_dir"
done