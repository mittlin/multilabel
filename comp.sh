g++ examples/cpp_classification/multilabel.cpp -o multilabel.bin -I/usr/lib/python2.7/dist-packages/numpy/core/include -I/usr/local/include -I.build_release/src -I./src -I./include -I/usr/local/cuda/include -lglog -lboost_system -L/usr/local/lib -lopencv_highgui  -lopencv_ml -lopencv_imgproc -lopencv_core -DUSE_OPENCV -std=c++11 -L./build/lib -lcaffe

./multilabel.bin examples/multilabel/resnet_50/deploy.prototxt examples/multilabel/snapshot/multilabel_resnet50_iter_1000.caffemodel examples/multilabel/train_mean.binaryproto examples/multilabel/label1.txt examples/multilabel/label2.txt /tmp/aa


