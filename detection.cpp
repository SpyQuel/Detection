#include <stdio.h>
#include "opencv/cv.h"
#include "opencv/highgui.h"

#define MEAN 23
#define MEDIAN 42
#define MIN_HIST 0
#define MAX_HIST 1
#define FIRST 99
#define LAST 88

typedef struct hist_struct {
	long int total;
	int values[256];
	int num;
} hist;

typedef struct rgb_struct {
	int R;
	int G;
	int B;
} rgb_s;

typedef struct blob_features_struct {
	long int area;
	long int perimeter4;
	long int perimeter8;
	char type[10];
} blob_features;

int open_avi();
void init_colors();
char * get_next_frame();
int display_image(char * win_name, IplImage * img, int delay);
int close_avi();
void release();
void frame_difference();
void elab();
void init_background(int mode);
void update_background();
void init_histogram(int num_frames);
int update_histogram(int i, int j, int ws, int value);
void add_element(int value, int * min_v, int * max_v);
void insert(int value, int * v);
int take(int index, int * v);
void find_blobs();

CvCapture * cap = 0;
char * filename;
char * win = 0;
IplImage * currentIpl = 0;
IplImage * currentImageGray = 0;
IplImage * background = 0;
IplImage * previous_frame = 0;
IplImage * previous_prev_frame = 0;
IplImage * frame_diff = 0;
IplImage * frame_diff1 = 0;
IplImage * frame_diff2 = 0;
IplImage * background_diff = 0;
IplImage * change_mask = 0;
IplImage * frame_blobs = 0;
IplImage * output = 0;
hist ** histograms;
rgb_s * colors;
char * winFrameDiff = 0;
char * winBG = 0;
char * winOut = 0;
char * winMask = 0;
char * winBGDiff = 0;
double  thresh = 13.0f;
double bg_thresh = 18.0f;
double alpha = 0.1f;
int frame_number;
int num_bg_frames;
int num_blobs;
FILE * file;

int main(int argc, char** argv) {

	char choice = '0';
	int res = 0;
	double new_val;
	double * mod_val = NULL;
	bool modify = false;
	printf("***********Change Detection***********\n");
	filename = "../data/video_proj.avi";
	
	while(choice!='g') { 
		fflush(stdin);
		printf("\nActual values :\n");
		printf("1 Three-Frames Difference Threshold : %.1Lf\n", thresh);
		printf("2 Background Updating Alpha : %.2Lf\n", alpha);
		printf("3 Background Difference Threshold : %.1Lf\n", bg_thresh);
		printf("Press g to start or a number to set the corresponding value (q to exit): ");
		scanf("%c",&choice);
		modify = false;
		switch(choice) {
			case 'q' : return -2;
			case '1' : {
							printf("Insert Three-Frames Difference Threshold : ");
							mod_val = &thresh;
							modify = true;
							break;
					   }
			case '2' : {
							printf("Insert Background Updating Alpha : ");
							mod_val = &alpha;
							modify = true;
							break;
					   }
			case '3' : {
							printf("Insert Background Difference Threshold : ");
							mod_val = &bg_thresh;
							modify = true;
							break;
					   }
		}
		if(modify) {
			res = scanf("%Lf",&new_val);
			if(res==1) {
				*mod_val=(double)new_val;
			}
			else {
				printf("value not valid; using defaults: %.1Lf\n",*mod_val);
			}
		}
	}

	res = 0;
	while(res!=1) {
		res = open_avi();
		
		if(res!=1) {
			printf("Insert file name (q to exit): ");
			scanf("%s",filename);
			if(strcmp(filename,"q")==0)
				return -1;
		}
	}
	
	char* im=0;
	frame_number=0;
	char retcode=0;

	init_colors();

	//background initialization
	num_bg_frames = 100;
	init_histogram(num_bg_frames);

	while((im = get_next_frame())!=0 && retcode!='q'){
		cvCvtColor(currentIpl,currentImageGray,CV_BGR2GRAY);

		if(frame_number<num_bg_frames) {
			frame_difference();
			init_background(MEDIAN);
		}
		else {
			elab();
			update_background();
			find_blobs();
		}

		if(retcode == 'f')
			retcode = (char)(display_image(win, currentIpl, 0));
		else
			retcode = (char)(display_image(win, currentIpl, 80));

		frame_number++;
		
	}

	printf("Press a key to exit..\n");
	cvWaitKey(0);
	close_avi();
	release();

	return 1;
}

void init_background(int mode) {

	if(frame_number==0) {
		background = cvCloneImage(currentImageGray);
		winBG = "Background";
		cvNamedWindow(winBG, 0);
		int i,j;
		int ws = background->widthStep;
		for(j=0; j<background->height; j++) {//row scanning
			for(i=0; i<background->width; i++) {//for each row: column scanning
				histograms[i][j].values[((unsigned char*)(background->imageData))[j*ws + i]]++;
				histograms[i][j].num++;
				histograms[i][j].total += ((unsigned char*)(background->imageData))[j*ws + i];
			}
		}
		return;
	}

	int i,j;
	int ws = background->widthStep;

	for(j=0; j<background->height; j++) {//row scanning
		for(i=0; i<background->width; i++) {//for each row: column scanning
			if(((unsigned char*)(frame_diff->imageData))[j*ws + i] == 0) {
				histograms[i][j].values[((unsigned char*)(currentImageGray->imageData))[j*ws + i]]++;
				histograms[i][j].num++;
				histograms[i][j].total += ((unsigned char*)(currentImageGray->imageData))[j*ws + i];
				if(mode==MEAN) {
					((unsigned char*)(background->imageData))[j*ws + i] = (histograms[i][j].total / histograms[i][j].num);
				}
				else if(mode==MEDIAN) {
					int k = 0, score = 0;
					while(score < (histograms[i][j].num/2)) {
						score += histograms[i][j].values[k];
						k++;
					}
					((unsigned char*)(background->imageData))[j*ws + i] = k;
				}
			}
		}
	}

	//Visualize
	cvResizeWindow(winBG,background->width,background->height);
	cvShowImage(winBG,background);
}

void update_background() {

	if(frame_number == num_bg_frames) {
		change_mask = cvCloneImage(currentImageGray);
		cvDestroyWindow(winFrameDiff);
		winMask = "Change Mask";
		cvNamedWindow(winMask, 0);
		return;
	}

	change_mask = cvCloneImage(background_diff);
	cvDilate(change_mask,change_mask,NULL,10);
	cvErode(change_mask,change_mask,NULL,3);

	//blind
	//cvAddWeighted(currentImageGray, alpha, background, 1-alpha, 0, background);

	//selective
	int i,j;
	int ws = background->widthStep;
	for(j=0; j<background->height; j++){//row scanning
		for(i=0; i<background->width; i++){//for each row: column scanning
			if(((unsigned char*)(change_mask->imageData))[j*ws + i] == 0) {
				((unsigned char*)(background->imageData))[j*ws + i] = 
					(((unsigned char*)(background->imageData))[j*ws + i]*(1-alpha) + ((unsigned char*)(currentImageGray->imageData))[j*ws + i]*alpha);
			}
		}
	}

	//Visualize
	cvResizeWindow(winBG,background->width,background->height);
	cvShowImage(winBG,background);

	cvResizeWindow(winMask,change_mask->width,change_mask->height);
	cvShowImage(winMask,change_mask);
}

void frame_difference() {
	
	if(frame_number==0) {
		CvSize in_size;
		in_size.height = currentIpl->height;
		in_size.width = currentIpl->width;
		previous_frame = cvCreateImage(in_size,IPL_DEPTH_8U,1);
		previous_prev_frame = cvCreateImage(in_size,IPL_DEPTH_8U,1);
		frame_diff = cvCreateImage(in_size,IPL_DEPTH_8U,1);
		frame_diff1 = cvCreateImage(in_size,IPL_DEPTH_8U,1);
		frame_diff2 = cvCreateImage(in_size,IPL_DEPTH_8U,1);
		winFrameDiff = "3-Frames Difference";
		cvNamedWindow(winFrameDiff, 0);

		background_diff = cvCreateImage(in_size,IPL_DEPTH_8U,1);

		//swap
		previous_frame = cvCloneImage(currentImageGray);
		previous_prev_frame = cvCloneImage(currentImageGray);
		return;
	}

	//difference between frame t-2 and t-1
	cvAbsDiff(previous_prev_frame,previous_frame,frame_diff1);
	cvThreshold(frame_diff1,frame_diff1,thresh,255,CV_THRESH_BINARY );
	cvErode(frame_diff1,frame_diff1,NULL,1);
	cvDilate(frame_diff1,frame_diff1,NULL,1);

	//difference between frame t-1 and t
	cvAbsDiff(currentImageGray,previous_frame,frame_diff2);
	cvThreshold(frame_diff2,frame_diff2,thresh,255,CV_THRESH_BINARY );
	cvErode(frame_diff2,frame_diff2,NULL,1);
	cvDilate(frame_diff2,frame_diff2,NULL,1);
	
	//common points are added to solution
	cvAddWeighted(frame_diff1, 0.5, frame_diff2, 0.5, 0, frame_diff);
	cvThreshold(frame_diff,frame_diff,200,255,CV_THRESH_BINARY );
	
	//visualize
	cvResizeWindow(winFrameDiff,frame_diff->width,frame_diff->height);
	cvShowImage(winFrameDiff,frame_diff);
	
	//swap
	previous_prev_frame = cvCloneImage(previous_frame);
	previous_frame = cvCloneImage(currentImageGray);
}

void elab() {

	winBGDiff = "BackGround Difference";
	cvNamedWindow(winBGDiff, 0);

	cvAbsDiff(currentImageGray,background,background_diff);
	cvThreshold(background_diff,background_diff,bg_thresh,255,CV_THRESH_BINARY);
	
	//con un kernel 2x6 ci sono alcuni punti casuali ma ovviamente la persona e gli oggetti sono piu definiti
	IplConvKernel * kernel_round = cvCreateStructuringElementEx(3,6,2,3,CV_SHAPE_ELLIPSE);

	cvErode(background_diff,background_diff,kernel_round,1);
	cvDilate(background_diff,background_diff,kernel_round,1);

	//Visualize
	cvResizeWindow(winBGDiff,background_diff->width,background_diff->height);
	cvShowImage(winBGDiff,background_diff);
}

void find_blobs() {

	frame_blobs = cvCloneImage(background_diff);

	if(frame_number == num_bg_frames) {
		CvSize in_size;
		in_size.height=frame_blobs->height;
		in_size.width=frame_blobs->width;
		output = cvCreateImage(in_size,IPL_DEPTH_8U,3);

		//final output
		winOut = "Change Detection";
		cvNamedWindow(winOut, 0);

		//file
		file = fopen("C:/Users/John/Desktop/output.txt", "w");
	}

	IplConvKernel * kernel = cvCreateStructuringElementEx(21,31,11,16,CV_SHAPE_ELLIPSE);

	cvDilate(frame_blobs,frame_blobs,kernel,1);
	cvErode(frame_blobs,frame_blobs,kernel,1);

	num_blobs = 0;
	int curr_blob = 0;
	int i,j,k,ws;
	int Bmax = 300;

	int * LUT;
	LUT = (int *)(malloc(sizeof(int)*Bmax));

	for(i=1;i<=Bmax;i++) LUT[i] = i;
	//PROBLEMA: Le IplImage* non ammettono valori superiori a 255 (valore mod 256) e quindi non si possono trovare piu
	//di 255 classi diverse altrimenti le classi sono scorrette
	ws = frame_blobs->widthStep;
	for(j=0; j<frame_blobs->height; j++){//row scanning
		for(i=0; i<frame_blobs->width; i++){//for each row: column scanning
			if(((unsigned char*)(frame_blobs->imageData))[j*ws + i] != 0) { 
				
				int lp = ((unsigned char*)(frame_blobs->imageData))[j*ws + i-1];
				
				int lq = ((unsigned char*)(frame_blobs->imageData))[(j-1)*ws + i];
				
				if(lp == 0 && lq == 0) {
					curr_blob++;
					((unsigned char*)(frame_blobs->imageData))[j*ws + i] = curr_blob;
					if(curr_blob > 255)
						printf("curr_blob = %d, value = %d\n", curr_blob, ((unsigned char*)(frame_blobs->imageData))[j*ws + i]);
				}
				else if(lp!=0 && lq!=0 && lp!=lq) {
					//equivalenza
					int c_lp = LUT[lp];
					for(k=1;k<Bmax;k++)
						if(LUT[k] == c_lp)
							LUT[k] = LUT[lq];
					((unsigned char*)(frame_blobs->imageData))[j*ws + i] = lq;
				}
				else if(lq != 0) {
					((unsigned char*)(frame_blobs->imageData))[j*ws + i] = lq;
				}
				else if(lp != 0) {
					((unsigned char*)(frame_blobs->imageData))[j*ws + i] = lp;
				}
			}
		}
	}

	int * already_seen = (int*) malloc(sizeof(int)*Bmax);

	for(i=1;i<=curr_blob;i++) {
		int found = 0;
		for(k=0;k<num_blobs;k++) {
			if(already_seen[k] == LUT[i]) found = 1;
		}
		if(found == 0) {
			already_seen[num_blobs++] = LUT[i];
		}
		for(j=0;j<num_blobs;j++) {
			if(LUT[i] == already_seen[j]) {
				LUT[i] = j+1;
			}
		}
	}

	blob_features * b_feats = (blob_features*) malloc(sizeof(blob_features)*num_blobs);
	for(i=0;i<num_blobs;i++) {
		b_feats[i].area = 0;
		b_feats[i].perimeter4 = 0;
		b_feats[i].perimeter8 = 0;
	}

	for(j=0; j<frame_blobs->height; j++){//row scanning
		for(i=0; i<frame_blobs->width; i++){//for each row: column scanning
			if(((unsigned char*)(frame_blobs->imageData))[j*ws + i] != 0) {
				int blob_v = LUT[((unsigned char*)(frame_blobs->imageData))[j*ws + i]];
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3] = colors[blob_v%10].R;
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3+1] = colors[blob_v%10].G;		
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3+2] = colors[blob_v%10].B;
				
				//calcolo degli attributi dei blob
				b_feats[blob_v-1].area++;
				if((((unsigned char*)(frame_blobs->imageData))[(j-1)*ws + i] == 0) ||
					(((unsigned char*)(frame_blobs->imageData))[(j+1)*ws + i] == 0) ||
					(((unsigned char*)(frame_blobs->imageData))[j*ws + i+1] == 0) ||
					(((unsigned char*)(frame_blobs->imageData))[j*ws + i-1] == 0)) {
						b_feats[blob_v-1].perimeter8++;
						b_feats[blob_v-1].perimeter4++;
				}
				else if((((unsigned char*)(frame_blobs->imageData))[(j-1)*ws + i-1] == 0) ||
					(((unsigned char*)(frame_blobs->imageData))[(j+1)*ws + i-1] == 0) ||
					(((unsigned char*)(frame_blobs->imageData))[(j-1)*ws + i+1] == 0) ||
					(((unsigned char*)(frame_blobs->imageData))[(j+1)*ws + i+1] == 0)) {
						b_feats[blob_v-1].perimeter4++;
				}
			}
			else {
				//sfondo dei blob
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3] = ((unsigned char*)(currentImageGray->imageData))[j*currentImageGray->widthStep + i];
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3+1] = ((unsigned char*)(currentImageGray->imageData))[j*currentImageGray->widthStep + i];		
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3+2] = ((unsigned char*)(currentImageGray->imageData))[j*currentImageGray->widthStep + i];

				//((unsigned char*)(output->imageData))[j*output->widthStep + i*3] = ((unsigned char*)(background->imageData))[j*background->widthStep + i];
				//((unsigned char*)(output->imageData))[j*output->widthStep + i*3+1] = ((unsigned char*)(background->imageData))[j*background->widthStep + i];		
				//((unsigned char*)(output->imageData))[j*output->widthStep + i*3+2] = ((unsigned char*)(background->imageData))[j*background->widthStep + i];
			}
		}
	}

	int actual_blob_num = 0;
	int blob_counter = 0;
	int max_area = 0;
	int max_area_index = -1;
	int min_area = 250;

	for(i=0;i<num_blobs;i++) {
		if(b_feats[i].area >= min_area) {
			actual_blob_num++;
			strcpy(b_feats[i].type, "object");
			if(b_feats[i].area > max_area) {
				max_area = b_feats[i].area;
				max_area_index = i;
			}
		}
	}
	if(max_area_index >= 0) {
		strcpy(b_feats[max_area_index].type, "person");
	}

	//filtro con area > 250!
	//elaborazione dati dei blob e salvataggio su file
	fprintf(file, "frame number: %d \t number of objects: %d\n", frame_number, actual_blob_num);
	for(i=0;i<num_blobs;i++) {
		if(b_feats[i].area >= min_area) {
			fprintf(file, "%d \t area: %5d \t perimeter: %4d \t ID : %s\n", ++blob_counter, b_feats[i].area, (int) (b_feats[i].perimeter8 + b_feats[i].perimeter4)/2, b_feats[i].type); 
		}
	}
	fprintf(file, "\n");

	cvResizeWindow(winOut,output->width,output->height);
	cvShowImage(winOut, output);

	free(b_feats);
	free(already_seen);

	//free(LUT);
}

void release() {
		cvReleaseImage(&previous_frame);
		cvReleaseImage(&currentImageGray);
		cvReleaseImage(&frame_diff);
		cvReleaseImage(&background);
		cvReleaseImage(&previous_prev_frame);
		cvReleaseImage(&frame_diff1);
		cvReleaseImage(&frame_diff2);
		cvReleaseImage(&background_diff);
		cvReleaseImage(&change_mask);
		cvReleaseImage(&output);
		cvReleaseImage(&frame_blobs);
		
		cvDestroyWindow(winOut);
		cvDestroyWindow(winBG);
		cvDestroyWindow(winMask);
		cvDestroyWindow(winBGDiff);
		cvDestroyWindow(winFrameDiff);

		free(histograms);
		free(colors);

		fclose(file);
}

int open_avi() {

	IplImage* ipl = 0;
	cap = cvCaptureFromAVI(filename);
	if(cap==0){
		printf("File %s was not found\n",filename);
		return -1;
	}
	win = filename;
	cvNamedWindow(win, 0);
	cvGrabFrame(cap);
	ipl = cvRetrieveFrame(cap);
	if(ipl->nChannels!=3) {
		printf("File %s contains images with nChannels!=3\n",filename);
		cvReleaseCapture(&cap);
		return -2;
	}
	currentIpl = cvCloneImage(ipl);
	currentIpl->origin = 0;

	CvSize in_size;
	in_size.height=currentIpl->height;
	in_size.width=currentIpl->width;
	currentImageGray=cvCreateImage(in_size,IPL_DEPTH_8U,1);
	cvCvtColor(currentIpl,currentImageGray,CV_BGR2GRAY);
	return 1;
}

char * get_next_frame() {

	IplImage* ipl = 0;
	int res = 0;
	char* imag = 0;
	if(frame_number>0) {
		res= cvGrabFrame(cap);
		if(res>0) {
			ipl = cvRetrieveFrame(cap);
			cvFlip(ipl,currentIpl);
			cvFlip(currentIpl);
			imag = currentIpl->imageData;
		}
	}
	else {
		imag = currentIpl->imageData;
	}
	return imag;
}

int display_image(char * win_name, IplImage * img, int delay) {
	
	cvResizeWindow(win_name,img->width,img->height);
	cvShowImage(win_name, img);

	if(delay > 0)
		return cvWaitKey(delay);
	else
		return cvWaitKey();
}

int close_avi() {

	if(cap!=0) cvReleaseCapture(&cap);
	if(currentIpl!=0)cvReleaseImage(&currentIpl);
	if(win!=0) {
		cvDestroyWindow(win);
		win=0;
	}
	return 1;
}

void init_histogram(int num_frames) {
	int i,j,k;
	int ws = currentIpl->widthStep;
	//initialization of structure
	histograms = (hist**)malloc(sizeof(hist*)*currentIpl->width);
	for(i=0;i<currentIpl->width;i++) {
		histograms[i] = (hist*)malloc(sizeof(hist)*currentIpl->height);
		for(j=0;j<currentIpl->height;j++) {
			histograms[i][j].total = 0;
			histograms[i][j].num = 0;
			for(k=0;k<256;k++) {
				histograms[i][j].values[k] = 0;
			}
		}
	}
}

void init_colors() {
	colors = (rgb_s *) malloc(sizeof(rgb_s)*10);
	
	colors[0].R = 57;
	colors[0].G = 0;
	colors[0].B = 0;

	colors[1].R = 0;
	colors[1].G = 255;
	colors[1].B = 0;

	colors[2].R = 0;
	colors[2].G = 0;
	colors[2].B = 255;

	colors[3].R = 255;
	colors[3].G = 0;
	colors[3].B = 0;

	colors[4].R = 122;
	colors[4].G = 0;
	colors[4].B = 122;

	colors[5].R = 0;
	colors[5].G = 122;
	colors[5].B = 122;

	colors[6].R = 122;
	colors[6].G = 0;
	colors[6].B = 0;

	colors[7].R = 0;
	colors[7].G = 122;
	colors[7].B = 0;

	colors[8].R = 0;
	colors[8].G = 0;
	colors[8].B = 122;

	colors[9].R = 255;
	colors[9].G = 0;
	colors[9].B = 122;
}
