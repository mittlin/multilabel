#include <caffe/caffe.hpp>
#ifdef USE_OPENCV
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#endif  // USE_OPENCV
#include <algorithm>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifdef USE_OPENCV
using namespace caffe;  // NOLINT(build/namespaces)
using std::string;
using std::ofstream;
using std::ifstream;
using std::cout;
using std::endl;

/* Pair (label, confidence) representing a prediction. */
// change: 2 classify ¡ª (label1, confidence1) (label2, confidence2)
typedef std::pair<string, float> Prediction;
class Classifier {
	public:
		Classifier(const string& model_file,
				const string& trained_file,
				const string& mean_file,
				const vector<string>& label_files);

		std::vector<vector<Prediction> > Classify(const cv::Mat& img, int N = 5);

	private:
		void SetMean(const string& mean_file);

		std::vector<vector<float> > Predict(const cv::Mat& img);

		void WrapInputLayer(std::vector<cv::Mat>* input_channels);

		void Preprocess(const cv::Mat& img,
				std::vector<cv::Mat>* input_channels);

	private:
		shared_ptr<Net<float> > net_;
		cv::Size input_geometry_;
		int num_channels_;
		cv::Mat mean_;
		std::vector<vector<string> > labels_; //multi
};

Classifier::Classifier(const string& model_file,
		const string& trained_file,
		const string& mean_file,
		const vector<string>& label_file) {
#ifdef CPU_ONLY
	Caffe::set_mode(Caffe::CPU);
#else
	Caffe::set_mode(Caffe::GPU);
#endif

	/* Load the network. */
	net_.reset(new Net<float>(model_file, TEST));
	net_->CopyTrainedLayersFrom(trained_file);

	CHECK_EQ(net_->num_inputs(), 1) << "Network should have exactly one input.";
	//CHECK_EQ(net_->num_outputs(), 1) << "Network should have exactly one output.";

	Blob<float>* input_layer = net_->input_blobs()[0];
	num_channels_ = input_layer->channels();
	CHECK(num_channels_ == 3 || num_channels_ == 1)
		<< "Input layer should have 1 or 3 channels.";
	input_geometry_ = cv::Size(input_layer->width(), input_layer->height());

	/* Load the binaryproto mean file. */
	SetMean(mean_file);

	/* Load labels. */
	//2 labels should read
	string line;
	for (int i = 0; i < label_file.size(); i++)
	{
		std::ifstream labels(label_file[i].c_str());
		CHECK(labels) << "Unable to open labels file " << label_file[i];
		vector<string> label_array;
		while (std::getline(labels, line))
		{
			label_array.push_back(line);
		}
		Blob<float>* output_layer = net_->output_blobs()[i];
		CHECK_EQ(label_array.size(), output_layer->channels())
			<< "Number of labels is different from the output layer dimension.";
		labels_.push_back(label_array);
	}
}

static bool PairCompare(const std::pair<float, int>& lhs,
		const std::pair<float, int>& rhs) {
	return lhs.first > rhs.first;
}

/* Return the indices of the top N values of vector v. */
static std::vector<int> Argmax(const std::vector<float>& v, int N) {
	std::vector<std::pair<float, int> > pairs;
	for (size_t i = 0; i < v.size(); ++i)
		pairs.push_back(std::make_pair(v[i], static_cast<int>(i)));
	std::partial_sort(pairs.begin(), pairs.begin() + N, pairs.end(), PairCompare);

	std::vector<int> result;
	for (int i = 0; i < N; ++i)
		result.push_back(pairs[i].second);
	return result;
}

/* Return the top N predictions. */
std::vector<vector<Prediction> > Classifier::Classify(const cv::Mat& img, int N) {
	auto output = Predict(img);
	int N1 = std::min<int>(labels_[0].size(), N);
	int N2 = std::min<int>(labels_[1].size(), N);
	std::vector<int> maxN1 = Argmax(output[0], N1);
	std::vector<int> maxN2 = Argmax(output[1], N2);
	std::vector<Prediction> predictions1;
	std::vector<Prediction> predictions2;

	for (int i = 0; i < N1; ++i) {
		int idx = maxN1[i];
		predictions1.push_back(std::make_pair(labels_[0][idx], output[0][idx]));
	}
	for (int i = 0; i < N2; ++i) {
		int idx = maxN2[i];
		predictions2.push_back(std::make_pair(labels_[1][idx], output[1][idx]));
	}
	vector<vector<Prediction> > predictions;
	predictions.push_back(predictions1);
	predictions.push_back(predictions2);
	return predictions;
}

/* Load the mean file in binaryproto format. */
void Classifier::SetMean(const string& mean_file) {
	BlobProto blob_proto;
	ReadProtoFromBinaryFileOrDie(mean_file.c_str(), &blob_proto);

	/* Convert from BlobProto to Blob<float> */
	Blob<float> mean_blob;
	mean_blob.FromProto(blob_proto);
	CHECK_EQ(mean_blob.channels(), num_channels_)
		<< "Number of channels of mean file doesn't match input layer.";

	/* The format of the mean file is planar 32-bit float BGR or grayscale. */
	std::vector<cv::Mat> channels;
	float* data = mean_blob.mutable_cpu_data();
	for (int i = 0; i < num_channels_; ++i) {
		/* Extract an individual channel. */
		cv::Mat channel(mean_blob.height(), mean_blob.width(), CV_32FC1, data);
		channels.push_back(channel);
		data += mean_blob.height() * mean_blob.width();
	}

	/* Merge the separate channels into a single image. */
	cv::Mat mean;
	cv::merge(channels, mean);

	/* Compute the global mean pixel value and create a mean image
	 * filled with this value. */
	cv::Scalar channel_mean = cv::mean(mean);
	mean_ = cv::Mat(input_geometry_, mean.type(), channel_mean);
}

std::vector<vector<float> > Classifier::Predict(const cv::Mat& img) {
	Blob<float>* input_layer = net_->input_blobs()[0];
	input_layer->Reshape(1, num_channels_,
			input_geometry_.height, input_geometry_.width);
	/* Forward dimension change to all layers. */
	net_->Reshape();

	std::vector<cv::Mat> input_channels;
	WrapInputLayer(&input_channels);

	Preprocess(img, &input_channels);

	net_->Forward();

	/* Copy the output layer to a std::vector */
	Blob<float>* output_layer1 = net_->output_blobs()[0];
	Blob<float>* output_layer2 = net_->output_blobs()[1];
	const float* begin1 = output_layer1->cpu_data();
	const float* end1 = begin1+ output_layer1->channels();
	const float* begin2 = output_layer2->cpu_data();
	const float* end2 = begin2 + output_layer2->channels();

	std::vector<float> prob1(begin1, end1);
	std::vector<float> prob2(begin2, end2);
	vector<vector<float> > prob_matrix;
	prob_matrix.push_back(prob1);
	prob_matrix.push_back(prob2);
	return prob_matrix;
}

/* Wrap the input layer of the network in separate cv::Mat objects
 * (one per channel). This way we save one memcpy operation and we
 * don't need to rely on cudaMemcpy2D. The last preprocessing
 * operation will write the separate channels directly to the input
 * layer. */
void Classifier::WrapInputLayer(std::vector<cv::Mat>* input_channels) {
	Blob<float>* input_layer = net_->input_blobs()[0];

	int width = input_layer->width();
	int height = input_layer->height();
	float* input_data = input_layer->mutable_cpu_data();
	for (int i = 0; i < input_layer->channels(); ++i) {
		cv::Mat channel(height, width, CV_32FC1, input_data);
		input_channels->push_back(channel);
		input_data += width * height;
	}
}

void Classifier::Preprocess(const cv::Mat& img,
		std::vector<cv::Mat>* input_channels) {
	/* Convert the input image to the input image format of the network. */
	cv::Mat sample;
	if (img.channels() == 3 && num_channels_ == 1)
		cv::cvtColor(img, sample, cv::COLOR_BGR2GRAY);
	else if (img.channels() == 4 && num_channels_ == 1)
		cv::cvtColor(img, sample, cv::COLOR_BGRA2GRAY);
	else if (img.channels() == 4 && num_channels_ == 3)
		cv::cvtColor(img, sample, cv::COLOR_BGRA2BGR);
	else if (img.channels() == 1 && num_channels_ == 3)
		cv::cvtColor(img, sample, cv::COLOR_GRAY2BGR);
	else
		sample = img;

	cv::Mat sample_resized;
	if (sample.size() != input_geometry_)
		cv::resize(sample, sample_resized, input_geometry_);
	else
		sample_resized = sample;

	cv::Mat sample_float;
	if (num_channels_ == 3)
		sample_resized.convertTo(sample_float, CV_32FC3);
	else
		sample_resized.convertTo(sample_float, CV_32FC1);

	cv::Mat sample_normalized;
	cv::subtract(sample_float, mean_, sample_normalized);

	/* This operation will write the separate BGR planes directly to the
	 * input layer of the network because it is wrapped by the cv::Mat
	 * objects in input_channels. */
	cv::split(sample_normalized, *input_channels);

	CHECK(reinterpret_cast<float*>(input_channels->at(0).data)
			== net_->input_blobs()[0]->cpu_data())
		<< "Input channels are not wrapping the input layer of the network.";
}

int main(int argc, char** argv) {
	if (argc != 7) {
		std::cerr << "Usage: " << argv[0]
			<< " deploy.prototxt network.caffemodel"
			<< " mean.binaryproto label1.txt label2.txt img.jpg" << std::endl;
		return 1;
	}

	::google::InitGoogleLogging(argv[0]);

	string model_file   = argv[1];
	string trained_file = argv[2];
	string mean_file    = argv[3];
	string label_file1   = argv[4];
	string label_file2   = argv[5];
	vector<string> label_file;
	label_file.push_back(label_file1);
	label_file.push_back(label_file2);
	std::cout << "the labels' channel:"<<label_file.size() << std::endl;
	Classifier classifier(model_file, trained_file, mean_file, label_file);

	struct timeval tpstart,tpend;
	float timeuse;
	gettimeofday(&tpstart,NULL);


	//read file form TXT
	ifstream testFile(argv[6]);
	string strtmp;
	vector<string> vec_imagename,vec_label1,vec_label2;
	while(getline(testFile, strtmp, '\n')){
		string sub_str = strtmp.substr(strtmp.find(' ') + 1, strtmp.size());
		vec_imagename.push_back(strtmp.substr(0, strtmp.find(' ')));
		vec_label1.push_back(sub_str.substr(0, sub_str.find(' ')));
		vec_label2.push_back(sub_str.substr(sub_str.find(' ') + 1, sub_str.size()));
	}

	int count_class1 = 0;
	int count_class2 = 0;
	float count_number = 0;
	float TotalNumberOfTestImages = vec_imagename.size();
	for (int j = 0; j < vec_imagename.size(); j++){
		string file = vec_imagename[j];
		std::cout << "-- Prediction for " << file << " --" << std::endl;

		cv::Mat img = cv::imread(file, -1);
		CHECK(!img.empty()) << "Unable to decode image " << file;
		auto predictions = classifier.Classify(img);

		/* Print the top 1 predictions. */
		Prediction p1 = predictions[0][0];
		std::cout << "Gender: " << std::fixed << std::setprecision(4)  << " \""
			<< p1.first << "\" - "  << p1.second  << std::endl;

		Prediction p2 = predictions[1][0];
		std::cout << "Race: " << std::fixed << std::setprecision(4)  << " \""
			<< p2.first << "\" - "  << p2.second  << std::endl;

		//string to number
		int predicted_label1, predicted_label2;
		stringstream predicted1(p1.first);
		predicted1 >> predicted_label1;
		stringstream predicted2(p2.first);
		predicted2 >> predicted_label2;

		int current_class1, current_class2;
		stringstream class1(vec_label1[j]);
		class1 >> current_class1;
		stringstream class2(vec_label2[j]);
		class2 >> current_class2;

		if(current_class1 == predicted_label1)
			count_class1 ++;
		if(current_class2 == predicted_label2)
			count_class2 ++;

		count_number = j+1 ;
		std::cout << "Count_Gender: " << count_class1 << " / " << count_number
			<< " = " << count_class1/count_number <<  std::endl;
		std::cout << "Count_Race: " << count_class2 << " / " << count_number
			<< " = " << count_class2/count_number <<  std::endl;
	}

	testFile.close();
	gettimeofday(&tpend,NULL);
	timeuse = 1000000*(tpend.tv_sec-tpstart.tv_sec)+tpend.tv_usec-tpstart.tv_usec;
	timeuse /= 1000000;
	std::cout << "\n\n---------------- Summury ----------- " 
		<< "Gender Accuracy: " << count_class1 << "/" << TotalNumberOfTestImages
		<< " = " << count_class1/TotalNumberOfTestImages
		<< "\nRace Accuracy: " << count_class2 << "/" << TotalNumberOfTestImages
		<< " = " << count_class2/TotalNumberOfTestImages
		<< "\nTime used: " << timeuse << " seconds." << std::endl;
}
#else
int main(int argc, char** argv) {
	LOG(FATAL) << "This example requires OpenCV; compile with USE_OPENCV.";
}
#endif  // USE_OPENCV
