#!/usr/bin/env sh
# Compute the mean image from the imagenet training lmdb
# N.B. this is available in data/age

EXAMPLE=examples/multilabel
DATA=examples/multilabel
TOOLS=build/tools/
$TOOLS/compute_image_mean $EXAMPLE/train_image_3race_lmdb \
  $DATA/train_3race_mean.binaryproto

$TOOLS/compute_image_mean $EXAMPLE/val_image_3race_lmdb \
  $DATA/val_3race_mean.binaryproto

echo "Done."
