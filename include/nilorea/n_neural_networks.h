/**\file n_neural_networks.h
 *  Neural network declarations
 *\author Castagnier Mickael
 *\version 1.0
 *\date 26/07/2015
 */
#ifndef N_NEURAL_NETWORKS

#define N_NEURAL_NETWORKS

#ifdef __cplusplus
extern "C"
{
#endif

#include "n_list.h"


#ifndef MIN
	#define MIN(x,y)      ((x)<(y) ? (x) : (y))
#endif

#ifndef MAX
	#define MAX(x,y)      ((x)>(y) ? (x) : (y))
#endif

#ifndef sqr
	#define sqr(x)        ((x)*(x))
#endif


/* randome value between int a and int b */
int rand_between_ints( int a , int b );    

/* randome value between double a and double b */
double rand_between_doubles( double a , double b );

double rand_equal_double( double a , double b );

int rand_equal_int( int a , int b );


/*! basic neural network activation function: sigmoid */
double n_neural_sigmoid ( double x ) ;


/*! Structure of a perceptron */
typedef struct N_PERCEPTRON
{
	/*! list of N_PERCEPTRON inputs for normal layer computing */
	LIST *intput_links,
	/*! list of N_PERCEPTRON inputs for signal generated layer computing */
	     *outputs_links ;
		
		/*! perceptron's bias */
		double bias_weights ,
		/*! perceptron activation value */ 
		treshold ,
		/*! computed output */
		output ,
		/*!  learning rate */
		delta ,
		/*! momentum */
		alpha ,
		/*! perceptron error */
		error  ;
		   
	/*! pointer to activation function */	   
	double(*a_func)(double val );
	
}N_PERCEPTRON ;


/*! One layer of percetron, size_x by size_y, some can be NULL ones */
typedef struct N_NEURAL_LAYER
{
	/*! Width of the layer */
	int size_x ,
	/*!	Height of the layer */
	size_y ;
	/*! Dynamically allocated array of perceptron. Some can be NULL. */
	N_PERCEPTRON **array ;
	
} N_NEURAL_LAYER ;



/*! structure of a Neural Network, a group of perceptron */
typedef struct NEURAL_NETWORK
{
	/*! name of the neural network */
	char *name ;

	/*! list of list of N_NEURAL_LAYER */
	LIST *network ;
	
	/*! pointer to input data */
	double **input ;
	
	/*! Width of input data, used to create first (input) layer */
	int input_x ,
	/*! Height of input data, used to create first (input) layer */
		input_y ,
	/*! current number of layers */
		nb_layers ,
	/*! Total number of active perceptrons */
		nb_total ;

	/*! computed output */
	double output ,
			/*! momentum */
			alpha ,
			/*! learning_rate */
			learning_rate ,
			/*! activation_func gain */
			gain ,
			/*! total of network errors */
			error ,
			/*! Ouput mid value (not sure) */
			Mean ,
			/*! Error level after training */
			TrainError ,
			/*! Mean Error level after training */
			TrainErrorPredictingMean ,
			/*! Error level after testing */			
			TestError ,
			/*! Mean Error level after testing */
			TestErrorPredictingMean;		
}NEURAL_NETWORK ;

N_PERCEPTRON *new_perceptron( double bias_weights , double treshold , double output , double delta , double momentum , double(*a_func)(double val ) );
		
/* at first call it will also create and set to NULL neural_net -> ptr , input_x , input_y  */
NEURAL_NETWORK *new_neural_network( int input_x , int input_y );

/* add a new layer */
int add_neural_net_layer( NEURAL_NETWORK *neural_net , int x , int y );
int set_perceptron( NEURAL_NETWORK *neural_net , int layer , int x , int y , int a_func , double bias , double treshold , double output );
int unset_perceptron( NEURAL_NETWORK *neural_net , int layer , int x , int y );
int add_perceptron_input( N_PERCEPTRON *n_perceptron , N_PERCEPTRON *input );
int neural_net_set_input( NEURAK_NETWORK *neural_net , double **input );
int neural_net_compute( NEURAL_NETWORK *neural_net , double *output );



#ifdef __cplusplus
}
#endif

#endif

/******************************************************************************
                     P R O P A G A T I N G   S I G N A L S
 ******************************************************************************/


void PropagateLayer(NET* Net, LAYER* Lower, LAYER* Upper)
{
  INT  i,j;
  REAL Sum;

  for (i=1; i<=Upper->Units; i++) {
    Sum = 0;
    for (j=0; j<=Lower->Units; j++) {
      Sum += Upper->Weight[i][j] * Lower->Output[j];
    }
    Upper->Output[i] = 1 / (1 + exp(-Net->Gain * Sum));
  }
}


void neural_net_compute(NET* Net)
{
  INT l;
   
  for (l=0; l<NUM_LAYERS-1; l++) {
    PropagateLayer(Net, Net->Layer[l], Net->Layer[l+1]);
  }
}

/******************************************************************************
                  B A C K P R O P A G A T I N G   E R R O R S
 ******************************************************************************/


void ComputeOutputError(NET* Net, REAL* Target)
{
  INT  i;
  REAL Out, Err;
   
  Net->Error = 0;
  for (i=1; i<=Net->OutputLayer->Units; i++) {
    Out = Net->OutputLayer->Output[i];
    Err = Target[i-1]-Out;
    Net->OutputLayer->Error[i] = Net->Gain * Out * (1-Out) * Err;
    Net->Error += 0.5 * sqr(Err);
  }
}


void BackpropagateLayer(NET* Net, LAYER* Upper, LAYER* Lower)
{
  INT  i,j;
  REAL Out, Err;
   
  for (i=1; i<=Lower->Units; i++) {
    Out = Lower->Output[i];
    Err = 0;
    for (j=1; j<=Upper->Units; j++) {
      Err += Upper->Weight[j][i] * Upper->Error[j];
    }
    Lower->Error[i] = Net->Gain * Out * (1-Out) * Err;
  }
}


void BackpropagateNet(NET* Net)
{
  INT l;
   
  for (l=NUM_LAYERS-1; l>1; l--) {
    BackpropagateLayer(Net, Net->Layer[l], Net->Layer[l-1]);
  }
}

void AdjustWeights(NET* Net)
{
  INT  l,i,j;
  REAL Out, Err, dWeight;
   
  for (l=1; l<NUM_LAYERS; l++) {
    for (i=1; i<=Net->Layer[l]->Units; i++) {
      for (j=0; j<=Net->Layer[l-1]->Units; j++) {
        Out = Net->Layer[l-1]->Output[j];
        Err = Net->Layer[l]->Error[i];
        dWeight = Net->Layer[l]->dWeight[i][j];
        Net->Layer[l]->Weight[i][j] += Net->Eta * Err * Out + Net->Alpha * dWeight;
        Net->Layer[l]->dWeight[i][j] = Net->Eta * Err * Out;
      }
    }
  }
}


/******************************************************************************
                      S I M U L A T I N G   T H E   N E T
 ******************************************************************************/


void SimulateNet(NET* Net, REAL* Input, REAL* Output, REAL* Target, BOOL Training)
{
  SetInput(Net, Input);
  neural_net_compute(Net);
  GetOutput(Net, Output);
   
  ComputeOutputError(Net, Target);
  if (Training) {
    BackpropagateNet(Net);
    AdjustWeights(Net);
  }
}


void TrainNet(NET* Net, INT Epochs)
{
  INT  Year, n;
  REAL Output[M];

  for (n=0; n<Epochs*TRAIN_YEARS; n++) {
    Year = RandomEqualINT(TRAIN_LWB, TRAIN_UPB);
    SimulateNet(Net, &(Sunspots[Year-N]), Output, &(Sunspots[Year]), TRUE);
  }
}


void TestNet(NET* Net)
{
  INT  Year;
  REAL Output[M];

  TrainError = 0;
  for (Year=TRAIN_LWB; Year<=TRAIN_UPB; Year++) {
    SimulateNet(Net, &(Sunspots[Year-N]), Output, &(Sunspots[Year]), FALSE);
    TrainError += Net->Error;
  }
  TestError = 0;
  for (Year=TEST_LWB; Year<=TEST_UPB; Year++) {
    SimulateNet(Net, &(Sunspots[Year-N]), Output, &(Sunspots[Year]), FALSE);
    TestError += Net->Error;
  }
  fprintf(f, "\nNMSE is %0.3f on Training Set and %0.3f on Test Set",
             TrainError / TrainErrorPredictingMean,
             TestError / TestErrorPredictingMean);
}


void EvaluateNet(NET* Net)
{
  INT  Year;
  REAL Output [M];
  REAL Output_[M];

  fprintf(f, "\n\n\n");
  fprintf(f, "Year    Sunspots    Open-Loop Prediction    Closed-Loop Prediction\n");
  fprintf(f, "\n");
  for (Year=EVAL_LWB; Year<=EVAL_UPB; Year++) {
    SimulateNet(Net, &(Sunspots [Year-N]), Output,  &(Sunspots [Year]), FALSE);
    SimulateNet(Net, &(Sunspots_[Year-N]), Output_, &(Sunspots_[Year]), FALSE);
    Sunspots_[Year] = Output_[0];
    fprintf(f, "%d       %0.3f                   %0.3f                     %0.3f\n",
               FIRST_YEAR + Year,
               Sunspots[Year],
               Output [0],
               Output_[0]);
  }
}


/******************************************************************************
                                    M A I N
 ******************************************************************************/


void main()
{
  NET  Net;
  BOOL Stop;
  REAL MinTestError;

  InitializeRandoms();
  GenerateNetwork(&Net);
  RandomWeights(&Net);
  InitializeApplication(&Net);

  Stop = FALSE;
  MinTestError = DBL_MAX;
  do {
    TrainNet(&Net, 10);
    TestNet(&Net);
    if (TestError < MinTestError) {
      fprintf(f, " - saving Weights ...");
      MinTestError = TestError;
      SaveWeights(&Net);
    }
    else if (TestError > 1.2 * MinTestError) {
      fprintf(f, " - stopping Training and restoring Weights ...");
      Stop = TRUE;
      RestoreWeights(&Net);
    }
  } while (NOT Stop);

  TestNet(&Net);
  EvaluateNet(&Net);
   
  FinalizeApplication(&Net);
}
Simulator Output for the Time-Series Forecasting Application

NMSE is 0.879 on Training Set and 0.834 on Test Set - saving Weights ...
NMSE is 0.818 on Training Set and 0.783 on Test Set - saving Weights ...
NMSE is 0.749 on Training Set and 0.693 on Test Set - saving Weights ...
NMSE is 0.691 on Training Set and 0.614 on Test Set - saving Weights ...
NMSE is 0.622 on Training Set and 0.555 on Test Set - saving Weights ...
NMSE is 0.569 on Training Set and 0.491 on Test Set - saving Weights ...
NMSE is 0.533 on Training Set and 0.467 on Test Set - saving Weights ...
NMSE is 0.490 on Training Set and 0.416 on Test Set - saving Weights ...
NMSE is 0.470 on Training Set and 0.401 on Test Set - saving Weights ...
NMSE is 0.441 on Training Set and 0.361 on Test Set - saving Weights ...
.
.
.
NMSE is 0.142 on Training Set and 0.143 on Test Set
NMSE is 0.142 on Training Set and 0.146 on Test Set
NMSE is 0.141 on Training Set and 0.143 on Test Set
NMSE is 0.146 on Training Set and 0.141 on Test Set
NMSE is 0.144 on Training Set and 0.141 on Test Set
NMSE is 0.140 on Training Set and 0.142 on Test Set
NMSE is 0.144 on Training Set and 0.148 on Test Set
NMSE is 0.140 on Training Set and 0.139 on Test Set - saving Weights ...
NMSE is 0.140 on Training Set and 0.140 on Test Set
NMSE is 0.141 on Training Set and 0.138 on Test Set - saving Weights ...
.
.
.
NMSE is 0.104 on Training Set and 0.154 on Test Set
NMSE is 0.102 on Training Set and 0.160 on Test Set
NMSE is 0.102 on Training Set and 0.160 on Test Set
NMSE is 0.100 on Training Set and 0.157 on Test Set
NMSE is 0.105 on Training Set and 0.153 on Test Set
NMSE is 0.100 on Training Set and 0.155 on Test Set
NMSE is 0.101 on Training Set and 0.154 on Test Set
NMSE is 0.100 on Training Set and 0.158 on Test Set
NMSE is 0.107 on Training Set and 0.170 on Test Set - stopping Training
                                                      and restoring Weights ...
NMSE is 0.141 on Training Set and 0.138 on Test Set


Year    Sunspots    Open-Loop Prediction    Closed-Loop Prediction

1960       0.572                   0.532                     0.532
1961       0.327                   0.334                     0.301
1962       0.258                   0.158                     0.146
1963       0.217                   0.156                     0.098
1964       0.143                   0.236                     0.149
1965       0.164                   0.230                     0.273
1966       0.298                   0.263                     0.405
1967       0.495                   0.454                     0.552
1968       0.545                   0.615                     0.627
1969       0.544                   0.550                     0.589
1970       0.540                   0.474                     0.464
1971       0.380                   0.455                     0.305
1972       0.390                   0.270                     0.191
1973       0.260                   0.275                     0.139
1974       0.245                   0.211                     0.158
1975       0.165                   0.181                     0.170
1976       0.153                   0.128                     0.175
1977       0.215                   0.151                     0.193
1978       0.489                   0.316                     0.274
1979       0.754                   0.622                     0.373

