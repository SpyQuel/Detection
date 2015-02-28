#include <stdio.h>
#include "opencv/cv.h"
#include "opencv/highgui.h"

#define MEAN 23
#define MEDIAN 42

//struttura di un istogramma
typedef struct hist_struct {
	//totale dei valori
	long int total;
	//vettore de singoli valori
	int values[256];
	//numero di valori inseriti
	int num;
} hist;
//struttura per la colorazione dei blob
typedef struct rgb_struct {
	int R;
	int G;
	int B;
} rgb_s;
//struttura con le caratteristiche dei blob
typedef struct blob_features_struct {
	//area
	long int area;
	//perimetro 4-connesso e 8-connesso
	long int perimeter4;
	long int perimeter8;
	//tag (tipo di soggetto)
	char type[20];
	//differenza tra blob e sfondo (per parte opzionale)
	double difference;
} blob_features;

int open_avi(char * filename);
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
void find_blobs(char * output_file);

CvCapture * cap = 0;
FILE * file;

//frame utilizzati per l'elaborazione
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

//istogrammi relativi ai singoli pixel
hist ** histograms;

//vettore di colori per i blob
rgb_s * colors;

//finestre di visualizzazione
char * win = 0;
char * winFrameDiff = 0;
char * winBG = 0;
char * winOut = 0;
char * winMask = 0;
char * winBGDiff = 0;

//parametri modificabili dall'utente
double  thresh = 13.0f;
double bg_thresh = 18.0f;
double alpha = 0.1f;

//frame corrente e numero di frame di background utilizzati
int frame_number;
int num_bg_frames;

int main(int argc, char** argv) {

	printf("***********Change Detection***********\n");
	//file video e file di testo di output
	char * filename = "../data/video_proj.avi";
	char * output_file = "output.txt";
	char choice = '0';
	int res = 0;
	double new_val;
	double * mod_val = NULL;
	bool modify = false;
	
	//menu di scelta
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

	//apertura del file video
	res = 0;
	while(res!=1) {
		res = open_avi(filename);
		
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

	//inizializzazione del vettore di colori
	init_colors();

	//inizializzazione background
	num_bg_frames = 100;
	init_histogram(num_bg_frames);

	//ripeto fino a che non termina il video o viene premuto q
	while((im = get_next_frame())!=0 && retcode!='q'){
		//estraggo il frame corrente (scala di grigi)
		cvCvtColor(currentIpl,currentImageGray,CV_BGR2GRAY);
		//se sono nei primi frame inizializzo il background
		if(frame_number<num_bg_frames) {
			//3-frame difference
			frame_difference();
			//inizializzazione tramite mediana dei valori degli istogrammi
			init_background(MEDIAN);
		}
		//altrimenti aggiorno il background ed estraggo i soggetti
		else {
			//trovo i soggetti tramite background difference
			elab();
			//aggiorno il background
			update_background();
			//associo ad ogni blob un valore (colore) e aggiorno il file
			find_blobs(output_file);
		}
		//se leggo il carattere f
		if(retcode == 'f')
			//visualizzo il frame corrente fino all'inserimento di un nuovo carattere
			retcode = (char)(display_image(win, currentIpl, 0));
		else
			//visualizzo il frame corrente per 80 ms poi procedo
			retcode = (char)(display_image(win, currentIpl, 80));
		//aggiorno il numero di frame corrente
		frame_number++;
		
	}

	printf("Press a key to exit..\n");
	cvWaitKey(0);
	//chiudo il file video
	close_avi();
	//rilascio tutte le risorse utilizzate
	release();

	return 1;
}

void init_background(int mode) {
	//alla prima invocazione
	if(frame_number==0) {
		//inizializzazione di immagini e finestre utilizzate
		background = cvCloneImage(currentImageGray);
		winBG = "Background";
		cvNamedWindow(winBG, 0);
		int i,j;
		//inizializzazione degli istogrammi
		int ws = background->widthStep;
		for(j=0; j<background->height; j++) {
			for(i=0; i<background->width; i++) {
				histograms[i][j].values[((unsigned char*)(background->imageData))[j*ws + i]]++;
				histograms[i][j].num++;
				histograms[i][j].total += ((unsigned char*)(background->imageData))[j*ws + i];
			}
		}
		return;
	}
	//aggiornamento dei valori degli istogrammi
	int i,j;
	int ws = background->widthStep;
	for(j=0; j<background->height; j++) {
		for(i=0; i<background->width; i++) {
			if(((unsigned char*)(frame_diff->imageData))[j*ws + i] == 0) {
				histograms[i][j].values[((unsigned char*)(currentImageGray->imageData))[j*ws + i]]++;
				histograms[i][j].num++;
				histograms[i][j].total += ((unsigned char*)(currentImageGray->imageData))[j*ws + i];
				//si aggiorna il valore di backround con la media dei valori
				if(mode==MEAN) {
					((unsigned char*)(background->imageData))[j*ws + i] = (histograms[i][j].total / histograms[i][j].num);
				}
				//o con la mediana dei valori
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

	//visualizzazione nelle finestre
	cvResizeWindow(winBG,background->width,background->height);
	cvShowImage(winBG,background);
}

void update_background() {
	//alla prima invocazione
	if(frame_number == num_bg_frames) {
		//si inizializzano immagini e finestre
		change_mask = cvCloneImage(currentImageGray);
		cvDestroyWindow(winFrameDiff);
		winMask = "Change Mask";
		cvNamedWindow(winMask, 0);
		return;
	}

	//la maschera viene creata dilatando la background difference per non prendere falsi negativi
	change_mask = cvCloneImage(background_diff);
	cvDilate(change_mask,change_mask,NULL,10);
	cvErode(change_mask,change_mask,NULL,3);

	//aggiornamento blind (non efficiente)
	//cvAddWeighted(currentImageGray, alpha, background, 1-alpha, 0, background);

	//aggiornamento selective
	int i,j;
	int ws = background->widthStep;
	for(j=0; j<background->height; j++){
		for(i=0; i<background->width; i++){
			if(((unsigned char*)(change_mask->imageData))[j*ws + i] == 0) {
				((unsigned char*)(background->imageData))[j*ws + i] = 
					(((unsigned char*)(background->imageData))[j*ws + i]*(1-alpha) + ((unsigned char*)(currentImageGray->imageData))[j*ws + i]*alpha);
			}
		}
	}

	//visualizzazione nelle finestre
	cvResizeWindow(winBG,background->width,background->height);
	cvShowImage(winBG,background);
	cvResizeWindow(winMask,change_mask->width,change_mask->height);
	cvShowImage(winMask,change_mask);
}

void frame_difference() {
	//alla prima invocazione
	if(frame_number==0) {
		//inizializzazione di frame e finestre
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

		//aggiornamento dei valori di frame t-1 e t-2
		previous_frame = cvCloneImage(currentImageGray);
		previous_prev_frame = cvCloneImage(currentImageGray);
		return;
	}

	//differenza tra frame t-2 e t-1
	cvAbsDiff(previous_prev_frame,previous_frame,frame_diff1);
	cvThreshold(frame_diff1,frame_diff1,thresh,255,CV_THRESH_BINARY );
	cvErode(frame_diff1,frame_diff1,NULL,1);
	cvDilate(frame_diff1,frame_diff1,NULL,1);

	//differenza tra frame t-1 e t
	cvAbsDiff(currentImageGray,previous_frame,frame_diff2);
	cvThreshold(frame_diff2,frame_diff2,thresh,255,CV_THRESH_BINARY );
	cvErode(frame_diff2,frame_diff2,NULL,1);
	cvDilate(frame_diff2,frame_diff2,NULL,1);
	
	//i punti comuni sono aggiunti alla soluzione
	cvAddWeighted(frame_diff1, 0.5, frame_diff2, 0.5, 0, frame_diff);
	cvThreshold(frame_diff,frame_diff,200,255,CV_THRESH_BINARY );
	
	//visualizzazione nelle finestre
	cvResizeWindow(winFrameDiff,frame_diff->width,frame_diff->height);
	cvShowImage(winFrameDiff,frame_diff);
	
	//aggiornamento dei valori di frame t-1 e t-2
	previous_prev_frame = cvCloneImage(previous_frame);
	previous_frame = cvCloneImage(currentImageGray);
}

void elab() {
	//alla prima invocazione
	if(frame_number == num_bg_frames) {
		//inizializzo la finestra
		winBGDiff = "BackGround Difference";
		cvNamedWindow(winBGDiff, 0);
	}

	//la differenza tra frame corrente e background (in valore assoluto) viene sottoposta a una soglia
	cvAbsDiff(currentImageGray,background,background_diff);
	cvThreshold(background_diff,background_diff,bg_thresh,255,CV_THRESH_BINARY);
	
	//creazione di uno structuring element ellittico verticale (forma allungata come i soggetti) 
	IplConvKernel * kernel_round = cvCreateStructuringElementEx(3,6,2,3,CV_SHAPE_ELLIPSE);
	//apertura per eliminare i punti spuri rilevati
	cvErode(background_diff,background_diff,kernel_round,1);
	cvDilate(background_diff,background_diff,kernel_round,1);

	//visualizzazione nelle finestre
	cvResizeWindow(winBGDiff,background_diff->width,background_diff->height);
	cvShowImage(winBGDiff,background_diff);
}

void find_blobs(char * output_file) {
	//per l'individuazione dei blob si parte dal background difference
	frame_blobs = cvCloneImage(background_diff);
	//alla prima invocazione
	if(frame_number == num_bg_frames) {
		//inizializzazione di immagini e finestre
		CvSize in_size;
		in_size.height=frame_blobs->height;
		in_size.width=frame_blobs->width;
		output = cvCreateImage(in_size,IPL_DEPTH_8U,3);
		winOut = "Change Detection";
		cvNamedWindow(winOut, 0);

		//apertura del file di testo di destinazione
		file = fopen(output_file, "w");
	}

	//creazione di uno structuring element ellittico verticale (forma allungata come i soggetti) 
	IplConvKernel * kernel = cvCreateStructuringElementEx(21,31,11,16,CV_SHAPE_ELLIPSE);
	//chiusura per delineare meglio la forma dei soggetti
	cvDilate(frame_blobs,frame_blobs,kernel,1);
	cvErode(frame_blobs,frame_blobs,kernel,1);

	int num_blobs = 0;
	int curr_blob = 0;
	int i,j,k,ws;
	int Bmax = 300;
	//inizializzazione della struttura di LookUp
	int * LUT;
	LUT = (int *)(malloc(sizeof(int)*Bmax));
	for(i=1;i<=Bmax;i++) LUT[i] = i;

	//primo scan per l'individuazione dei blob
	//PROBLEMA: Le IplImage* non ammettono valori superiori a 255 (valore mod 256) e quindi non si possono trovare piu
	//di 255 classi diverse (altrimenti le classi sono scorrette); una soluzione potrebbe essere quella di usare una
	//matrice height x width di interi al posto di un'IplImage* per poter avere valori maggiori. Per il video in esame
	//255 valori sono comunque sufficienti e quindi non abbiamo tale problema
	ws = frame_blobs->widthStep;
	for(j=0; j<frame_blobs->height; j++){
		for(i=0; i<frame_blobs->width; i++){
			if(((unsigned char*)(frame_blobs->imageData))[j*ws + i] != 0) { 
				//pixel nella riga precedente, stessa colonna
				int lp = ((unsigned char*)(frame_blobs->imageData))[j*ws + i-1];
				//pixel nella colonna precedente, stessa riga
				int lq = ((unsigned char*)(frame_blobs->imageData))[(j-1)*ws + i];
				//se lp e lq sono di sfondo il pixel corrente ha un nuovo valore
				if(lp == 0 && lq == 0) {
					curr_blob++;
					((unsigned char*)(frame_blobs->imageData))[j*ws + i] = curr_blob;
				}
				//caso di equivalenza tra lp e lq
				else if(lp!=0 && lq!=0 && lp!=lq) {
					//aggiorno l'equivalenza direttamente invece di utilizzare una matrice come del 2-scan tradizionale
					int c_lp = LUT[lp];
					for(k=1;k<Bmax;k++)
						if(LUT[k] == c_lp)
							LUT[k] = LUT[lq];
					((unsigned char*)(frame_blobs->imageData))[j*ws + i] = lq;
				}
				//se solo lq non di sfondo ne propago il valore
				else if(lq != 0) {
					((unsigned char*)(frame_blobs->imageData))[j*ws + i] = lq;
				}
				//allo stesso modo per lp
				else if(lp != 0) {
					((unsigned char*)(frame_blobs->imageData))[j*ws + i] = lp;
				}
			}
		}
	}

	//aggiorno i valori della LookUp Table in modo da evre tutti valori sequenziali
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

	//inizializzo il vettore delle caratteristiche dei blob
	blob_features * b_feats = (blob_features*) malloc(sizeof(blob_features)*num_blobs);
	for(i=0;i<num_blobs;i++) {
		b_feats[i].area = 0;
		b_feats[i].perimeter4 = 0;
		b_feats[i].perimeter8 = 0;
		b_feats[i].difference = 0;
	}
	
	//aggiorno i valori dei blob
	for(j=0; j<frame_blobs->height; j++){
		for(i=0; i<frame_blobs->width; i++){
			if(((unsigned char*)(frame_blobs->imageData))[j*ws + i] != 0) {
				//associo un colore tramite il numero sequenziale trovato
				int blob_v = LUT[((unsigned char*)(frame_blobs->imageData))[j*ws + i]];
				((unsigned char*)(frame_blobs->imageData))[j*ws + i] = blob_v;
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3] = colors[blob_v%10].R;
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3+1] = colors[blob_v%10].G;		
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3+2] = colors[blob_v%10].B;
				
				//calcolo degli attributi dei blob
				//area
				b_feats[blob_v-1].area++;
				//perimetri 4 e 8 connessi
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
				//aggiorno lo sfondo dell'immagine a colori dei blob con lo sfondo corrente
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3] = ((unsigned char*)(currentImageGray->imageData))[j*currentImageGray->widthStep + i];
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3+1] = ((unsigned char*)(currentImageGray->imageData))[j*currentImageGray->widthStep + i];		
				((unsigned char*)(output->imageData))[j*output->widthStep + i*3+2] = ((unsigned char*)(currentImageGray->imageData))[j*currentImageGray->widthStep + i];
			}
		}
	}

	int actual_blob_num = 0;
	int blob_counter = 0;
	int max_area = 0;
	int max_area_index = -1;
	int num_objects = 0;
	//prendo solo blob con area maggiore di una soglia (non prendo blob spuri)
	int min_area = 250;
	//inizializzo i tag dei blob a "oggetto"
	for(i=0;i<num_blobs;i++) {
		if(b_feats[i].area >= min_area) {
			actual_blob_num++;
			strcpy(b_feats[i].type, "object");
			num_objects ++;
			if(b_feats[i].area > max_area) {
				max_area = b_feats[i].area;
				max_area_index = i;
			}
		}
	}
	//associo al blob con area maggiore il tag "persona"
	if(max_area_index >= 0) {
		strcpy(b_feats[max_area_index].type, "person");
		num_objects--;
	}

	//parte opzionale, solo se trovo almeno due oggetti
	if(num_objects > 1) {
		for(j=0; j<frame_blobs->height; j++) {
			for(i=0; i<frame_blobs->width; i++) {
				//per ogni oggetto viene salvata la somma delle differenze quadrate tra i pixel all'interno del blob
				//e quelli all'esterno (5 pixel come offset) nel frame originale
				if((((unsigned char*)(frame_blobs->imageData))[j*ws + i] != 0) &&
					(strcmp(b_feats[((unsigned char*)(frame_blobs->imageData))[j*ws + i] - 1].type,"object")==0)) {
					if(((unsigned char*)(frame_blobs->imageData))[(j-1)*ws + i] == 0) {
						b_feats[((unsigned char*)(frame_blobs->imageData))[j*ws + i] - 1].difference +=
							(((unsigned char*)(currentImageGray->imageData))[(j-5)*ws + i] - ((unsigned char*)(currentImageGray->imageData))[(j+5)*ws + i])*(((unsigned char*)(currentImageGray->imageData))[(j-5)*ws + i] - ((unsigned char*)(currentImageGray->imageData))[(j+5)*ws + i]);
					}
					if(((unsigned char*)(frame_blobs->imageData))[(j+1)*ws + i] == 0) {
						b_feats[((unsigned char*)(frame_blobs->imageData))[j*ws + i] - 1].difference +=
							(((unsigned char*)(currentImageGray->imageData))[(j-5)*ws + i] - ((unsigned char*)(currentImageGray->imageData))[(j+5)*ws + i])*(((unsigned char*)(currentImageGray->imageData))[(j-5)*ws + i] - ((unsigned char*)(currentImageGray->imageData))[(j+5)*ws + i]);
					}
					if(((unsigned char*)(frame_blobs->imageData))[j*ws + i-1] == 0) {
						b_feats[((unsigned char*)(frame_blobs->imageData))[j*ws + i] - 1].difference +=
							(((unsigned char*)(currentImageGray->imageData))[j*ws + i-5] - ((unsigned char*)(currentImageGray->imageData))[j*ws + i+5])*(((unsigned char*)(currentImageGray->imageData))[j*ws + i-5] - ((unsigned char*)(currentImageGray->imageData))[j*ws + i+5]);
					}
					if(((unsigned char*)(frame_blobs->imageData))[j*ws + i+1] == 0) {
						b_feats[((unsigned char*)(frame_blobs->imageData))[j*ws + i] - 1].difference +=
							(((unsigned char*)(currentImageGray->imageData))[j*ws + i-5] - ((unsigned char*)(currentImageGray->imageData))[j*ws + i+5])*(((unsigned char*)(currentImageGray->imageData))[j*ws + i-5] - ((unsigned char*)(currentImageGray->imageData))[j*ws + i+5]);
					}
				}
			}
		}
		//trovo il blob con massima differenza tra pixel interni ed esterni
		double max_difference = 0;
		for(i=0;i<num_blobs;i++) {
			if(b_feats[i].difference > max_difference) {
				max_difference = b_feats[i].difference; 
			}
		}
		//assegno al blob con massima differenza il tag true e all'altro il tag false
		for(i=0;i<num_blobs;i++) {
			if(b_feats[i].difference > 0) {
				if(b_feats[i].difference == max_difference) 
					strcpy(b_feats[i].type, "true object");
				else
					strcpy(b_feats[i].type, "false object");
			}
		}
	}

	//elaborazione dati dei blob e salvataggio su file
	fprintf(file, "frame number: %d \t number of objects: %d\n", frame_number, actual_blob_num);
	for(i=0;i<num_blobs;i++) {
		if(b_feats[i].area >= min_area) {
			fprintf(file, "%d \t area: %5d \t perimeter: %4d \t ID : %s\n", ++blob_counter, b_feats[i].area, (int) (b_feats[i].perimeter8 + b_feats[i].perimeter4)/2, b_feats[i].type); 
		}
	}
	fprintf(file, "\n");

	//visualizazzione nelle finestre
	cvResizeWindow(winOut,output->width,output->height);
	cvShowImage(winOut, output);

	//libero le strutture utilizzate
	free(b_feats);
	free(already_seen);
}

void release() {
	//rilascio le immagini utilizzate
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
	//rilascio le finestre di visualizzazione
	cvDestroyWindow(winOut);
	cvDestroyWindow(winBG);
	cvDestroyWindow(winMask);
	cvDestroyWindow(winBGDiff);
	cvDestroyWindow(winFrameDiff);
	//libero la memoria delle strutture
	free(histograms);
	free(colors);
	//chiudo il file
	fclose(file);
}

int open_avi(char * filename) {
	//apro il file video
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
	//copio l'immagine estratta per non corrompere il video originale!
	currentIpl = cvCloneImage(ipl);
	currentIpl->origin = 0;
	//estraggo l'immagine in scala di grigi
	CvSize in_size;
	in_size.height=currentIpl->height;
	in_size.width=currentIpl->width;
	currentImageGray=cvCreateImage(in_size,IPL_DEPTH_8U,1);
	cvCvtColor(currentIpl,currentImageGray,CV_BGR2GRAY);
	return 1;
}

char * get_next_frame() {
	//estraggo il frame successivo
	IplImage* ipl = 0;
	int res = 0;
	char* imag = 0;
	if(frame_number>0) {
		res= cvGrabFrame(cap);
		//copio l'immagine estratta per non corrompere il video originale!
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
	//visualizzo l'immagien corrispondente
	cvResizeWindow(win_name,img->width,img->height);
	cvShowImage(win_name, img);
	//per delay millisecondi
	if(delay > 0)
		return cvWaitKey(delay);
	//o fino all'immissione di un nuovo carattere
	else
		return cvWaitKey();
}

int close_avi() {
	//chiudo il file video
	if(cap!=0) cvReleaseCapture(&cap);
	if(currentIpl!=0)cvReleaseImage(&currentIpl);
	if(win!=0) {
		cvDestroyWindow(win);
		win=0;
	}
	return 1;
}

void init_histogram(int num_frames) {
	//inizializzazione degli istogrammi
	int i,j,k;
	int ws = currentIpl->widthStep;
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
	//inizializzazione del vettore di colori con valori cablati
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
