
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time/posix_time/posix_time.hpp> //include all types plus i/o

#include <boost/thread.hpp>
#include <boost/container/vector.hpp>

#include "keyCandidateDistributor.h"

#include <boost/asio.hpp>
#include <csignal>

#include "OptionPrinter.h"

#include "gpu_code.h"
#include "cpu_code.h"

#include <stdio.h>
#include <string.h>

#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

#include "salsa20.h"
#include "petya.h"

#define VERBOSE 0


#include <iostream>

namespace po = boost::program_options;
using namespace std;

bool shutdownRequested = false;

unsigned long long nrOfKeysSearched = 0;
char* veribuf;
char* nonce;

bool keyFound = false;

bool tryKey(char *key) {
      bool veribufIsValid = false;
      char veribuf_test[VERIBUF_SIZE];
      
    memcpy(veribuf_test, veribuf, VERIBUF_SIZE);
      
      if (s20_crypt((uint8_t *) key, S20_KEYLEN_128, (uint8_t *) nonce, 0, (uint8_t *) veribuf_test, VERIBUF_SIZE) == S20_FAILURE) {
          puts("Error: encryption failed");
          return false;
      }
      veribufIsValid = is_valid(veribuf_test);
      
      if (veribufIsValid) {
        return true;
      } else {
        return false;
      }
}





boost::asio::io_service io_service;
boost::asio::signal_set signals(io_service, SIGINT, SIGTERM);
void handler(
    const boost::system::error_code& error,
    int signal_number)
{

  // cout << "Signal handler" <<endl;

  if (!error)
  {
    // A signal occurred.
	  switch (signal_number) {
	  case SIGTERM:   shutdownRequested = true;
	  	  	  	  	  //cout << "SIGTERM received" << endl;
	  	  	  	  	  break;
	  case SIGINT:   shutdownRequested = true;
	  	  	  	  	  // ctr+c pressed
	  	  	  	  	  //cout << "SIGINT received" << endl;
	  	  	  	  	  break;
	  default:       signals.async_wait(handler);
		  	  	     break;
	  }

//	  cout << "Signal occured" <<endl;
  }

}

void setupSignalHandler() {

	// io_service.poll();

	// Construct a signal set registered for process termination.
	// Start an asynchronous wait for one of the signals to occur.
	//boost::bind(&handler, this);
	signals.async_wait(handler);
	io_service.run();
    // io_service.poll();

}

void printTimeEstimation(unsigned long long keysCalculated, unsigned long long nrOfSecondsInTotalMeasured) {
	unsigned long long totalSecondsToCalculateAllKeys = (pow(2*26+10,8) / keysCalculated)*nrOfSecondsInTotalMeasured;
	unsigned long long years = totalSecondsToCalculateAllKeys /60/60/24/365;
	unsigned long long days = (totalSecondsToCalculateAllKeys /60/60/24)-years*365;
	unsigned long long hours = (totalSecondsToCalculateAllKeys /60/60)-(years*365*24+days*24);
	unsigned long long minutes = (totalSecondsToCalculateAllKeys /60)-(years*365*24*60+days*24*60+hours*60);

	std::cout << years << " years" << endl;
	std::cout << days << " days" << endl;
	std::cout << hours << " hours" << endl;
	std::cout << minutes << " minutes" << endl;

}

void checkShutdownRequested(){
	if (shutdownRequested){
		io_service.stop();
		cout << "Program interrupted" << endl;
		exit(0);
	}
}

int main(int argc, char *argv[])
{
	unsigned long long totalKeyRange = 2 * 26 + 10;
	
	for (int i = 0; i < 7; i++) {
		totalKeyRange *= 2 * 26 + 10;
	}
	// (unsigned long)pow((2 * 26 + 10), 8);

	std::string appName = boost::filesystem::basename(argv[0]);

	bool enableCPU;
	bool enableGPU;

	po::options_description commandLineOptions;

	po::options_description generic("Options");
	generic.add_options()
	    ("help", "display help message")
		("version", "output the version number")
	    ("file", po::value<string>(), "filename which contains disk dump of crypted harddrive (does only need to be the first 57 sectors)")
	    ("gpu", po::bool_switch(&enableGPU)->default_value(true), "keys are calculated on gpu (notice if you specify both option gpu and cpu is used to calculute the keys)")
	    ("cpu", po::bool_switch(&enableCPU)->default_value(false), "keys are calculated on cpu")

		("resume", "resume previous calculation")
		("random", "use a random key instead of brute force mothod, notice this doesn't allow a resume as wrong keys are not stored")
	    ("key", po::value<string>(), "try a specific key")
	    ("selftest", "check if algorithms work")
	    ("performance", "provide information about performance")

	;

	po::options_description optionalGPU("Optional GPU Arguments");
	optionalGPU.add_options()
		("gpu_threads", po::value<unsigned int>()->default_value(1024), "number of threads to use on GPU")
	    ("gpu_blocks", po::value<unsigned int>()->default_value(1), "number of blocks to use on GPU")
	    ("gpu_keysCtxSwitch", po::value<unsigned long long>()->default_value(10000), "number keys which are calculated on a the gpu before the context switches back to host")
	;

	po::options_description optionalCPU("Optional GPU Arguments");
	optionalCPU.add_options()
	    ("cpu_threads", po::value<unsigned int>()->default_value(10), "nr of threads to use on CPU for CPU calculation")
	;

	po::options_description optionalGeneric("Optional Generic Arguments");
	optionalGeneric.add_options()
		("start_key", po::value<unsigned long long>()->default_value(0), "start key number (defaults to 0)")
	    ("nrOfKeysToCalculate", po::value<unsigned long long>()->default_value(totalKeyRange), "nr of keys which should be calculated before program ends [defaults to all key combinations (2*26+10)^8]")
	;

	po::positional_options_description positionalOptions;
	positionalOptions.add("file", 1);

	commandLineOptions.add(generic).add(optionalCPU).add(optionalGPU).add(optionalGeneric);


	boost::thread signalHUPThread(setupSignalHandler);

	po::variables_map vm;

	try {
		// po::store(po::parse_command_line(argc, argv, commandLineOptions), vm);
		po::store(po::command_line_parser(argc, argv).options(commandLineOptions)
		            .positional(positionalOptions).run(), vm);

		po::notify(vm);

		if (vm.count("help")) {

			rad::OptionPrinter::printStandardAppDesc(appName,
															 std::cout,
															 commandLineOptions,
															 &positionalOptions);

			return 1;
		}

		if (vm.count("version")) {
			cout << appName << " Version 1.0" << endl;
			return 0;
		}

		if (vm.count("performance")) {
			cout << "There are " << pow(26*2+10,8) << " candidates to try." << endl;
			cout << "All keys together stored on the harddrive would take " << (unsigned int)((pow(26*2+10,8)*8)/1024/1024/1024/1024) << " terabytes of data [(2*26+10)^8*8)/1 TB]"<< endl;
			cout << "The odds to find the key by guessing is 1 in 218340105584896" << endl << endl;

			cout << "However based upon your current selected configuration and hardware I try to do a rough time estimation for a brute force attack" << endl;

			unsigned long long ctxSwitchKeys = vm["gpu_keysCtxSwitch"].as<unsigned long long>();
			unsigned long long nrThreads = vm["gpu_threads"].as<unsigned int>();
			unsigned long long nrBlocks = vm["gpu_blocks"].as<unsigned int>();

			unsigned long long nrOfGPUKeysCalculated = 0;
			unsigned long long nrOfSecondsInTotalMeasuredOnGPU =0;

			cout << endl;
			cout << "Performing some tests now. This will take a up to 5 minutes..." << endl;
		    cout << endl;
			cout << "Launching Performance Test with "<< endl;
			cout << " Blocks....................................... "<<nrBlocks  << endl;
			cout << " Threads...................................... " << nrThreads << endl;
			cout << " Keys calculated before GPU context returns... "<< ctxSwitchKeys  << endl;


			measureGPUPerformance(nrBlocks,
			        nrThreads,
					ctxSwitchKeys, &nrOfGPUKeysCalculated, &nrOfSecondsInTotalMeasuredOnGPU, &shutdownRequested, 30);

			std::cout << "Based upon this performance all keys will be calculated on GPU standalone in " << endl;

			printTimeEstimation(nrOfGPUKeysCalculated, nrOfSecondsInTotalMeasuredOnGPU);

			unsigned long long cpuThreads = vm["cpu_threads"].as<unsigned int>();

			unsigned long long nrOfCPUKeysCalculated = 0;
			unsigned long long nrOfSecondsInTotalMeasuredOnCPU =0;
			measureCPUPerformance(cpuThreads, &nrOfCPUKeysCalculated, &nrOfSecondsInTotalMeasuredOnCPU, &shutdownRequested, 30);

			checkShutdownRequested();

			cout << endl;
			std::cout << "and on CPU alone in " << endl;
			printTimeEstimation(nrOfCPUKeysCalculated, nrOfSecondsInTotalMeasuredOnCPU);

			cout << endl;
			std::cout << "Now testing GPU and CPU in combination..." << endl;

			vector<boost::thread> threadList;


			threadList.push_back(boost::thread(measureGPUPerformance,
											   nrBlocks,
   											   nrThreads,
											   ctxSwitchKeys,
											   &nrOfGPUKeysCalculated,
											   &nrOfSecondsInTotalMeasuredOnGPU,
											   &shutdownRequested,
											   30));

			threadList.push_back(boost::thread(measureCPUPerformance,
											   cpuThreads,
											   &nrOfCPUKeysCalculated,
											   &nrOfSecondsInTotalMeasuredOnCPU,
											   &shutdownRequested,
											   30));

			

			for (unsigned int i=0; i<threadList.size(); i++) {
				threadList[i].join();
			}

			checkShutdownRequested();

			// TODO: should be equalized with durations...
			printTimeEstimation(nrOfCPUKeysCalculated+nrOfGPUKeysCalculated, nrOfSecondsInTotalMeasuredOnCPU);


			return 0;
		}

		char p_key[KEY_SIZE+1];
		char *key = p_key;


		bool selftest = false;

		if (vm.count("selftest")) {
			selftest = true;

			nonce = (char *)malloc(8);
			veribuf = (char *)malloc(VERIBUF_SIZE);

			nonce[0] = 0x07;
			nonce[1] = 0x0c;
			nonce[2] = 0x12;
			nonce[3] = 0xf6;
			nonce[4] = 0x79;
			nonce[5] = 0x28;
			nonce[6] = 0x73;
			nonce[7] = 0xcb;

			veribuf[0] = 0x34;
			veribuf[1] = 0x80;
			veribuf[2] = 0x15;
			veribuf[3] = 0x1a;
			veribuf[4] = 0xd1;
			veribuf[5] = 0x76;
			veribuf[6] = 0x5c;
			veribuf[7] = 0x7b;
			veribuf[8] = 0x60;
			veribuf[9] = 0x2b;
			veribuf[10] = 0xe3;
			veribuf[11] = 0xd0;
			veribuf[12] = 0xd0;
			veribuf[13] = 0xae;
			veribuf[14] = 0xf8;
			veribuf[15] = 0xc2;

			p_key[0] = 'n';
			p_key[1] = 'G';
			p_key[2] = 'u';
			p_key[3] = 'J';
			p_key[4] = 'G';
			p_key[5] = 'b';
			p_key[6] = 'm';
			p_key[7] = 'D';
			p_key[8] = 'u';
			p_key[9] = 'V';
			p_key[10] = 'N';
			p_key[11] = '9';
			p_key[12] = 'X';
			p_key[13] = 'm';
			p_key[14] = 'L';
			p_key[15] = 'a';
			p_key[16] = 0;


			printf("verification data:\n");
			hexdump(veribuf, VERIBUF_SIZE);

			printf("nonce:\n");
			hexdump(nonce,NONCE_SIZE);
			printf("---\n");

			printf("Performing CPU test.....");

			bool verifbusIsValid = tryKey(p_key);
			if (verifbusIsValid) printf(" passed\r\n");
			else printf(" failed\r\n");

			printf("Performing GPU test.....");

			unsigned int gpuThreads = vm["gpu_threads"].as<unsigned int>();;
			unsigned int gpuBlocks = vm["gpu_blocks"].as<unsigned int>();;

			unsigned int nrKeys = gpuThreads*gpuBlocks;
			char *keys = (char *) malloc(nrKeys*sizeof(char)*KEY_SIZE);
			bool *result = (bool *) malloc(nrKeys*sizeof(bool)*KEY_SIZE);

			bool gpuPassed = true;
			for (int i=0; i<nrKeys; i++) {
				memset(keys, 0, nrKeys*sizeof(char)*KEY_SIZE);
				memcpy(keys+i*KEY_SIZE, p_key, KEY_SIZE);
				tryKeysGPUSingleShot(gpuBlocks, gpuThreads, (uint8_t *)nonce, veribuf, keys, nrKeys, result);

				for (int j=0; j<nrKeys;j++) {
					if (j!=i && (result[j]==true)) {
						gpuPassed = false;
						break;
					}
					if (j==i && (result[j]==false)) {
						gpuPassed = false;
						break;
					}
				}
			}

			if (gpuPassed) {

				unsigned int gpuThreads = vm["gpu_threads"].as<unsigned int>();;
				unsigned int gpuBlocks = vm["gpu_blocks"].as<unsigned int>();;
				unsigned long long ctxSwitchKeys = vm["gpu_keysCtxSwitch"].as<unsigned long long>();

				unsigned int nrKeys = gpuThreads*gpuBlocks;
				char *keys = (char *)malloc(nrKeys*sizeof(char)*KEY_SIZE);
				bool *result = (bool *)malloc(nrKeys*sizeof(bool)*KEY_SIZE);

				unsigned long long startKey = vm["start_key"].as<unsigned long long>();
				unsigned long long nrOfKeysToCalculate = vm["nrOfKeysToCalculate"].as<unsigned long long>();

				unsigned int currentKeyIndex = startKey;
				char *currentKey = keys;

				unsigned long long blockSize = nrOfKeysToCalculate / nrKeys;

				if (blockSize == 0) blockSize = 1;

				for (int i = 0; i<nrKeys; i++) {
					calculate16ByteKeyFromIndex(currentKeyIndex, currentKey);
					currentKey += KEY_SIZE;
					currentKeyIndex += nrOfKeysToCalculate / nrKeys;
				}

				/*
				keys[0] = 'n';
				keys[1] = 'G';
				keys[2] = 'u';
				keys[3] = 'J';
				keys[4] = 'G';
				keys[5] = 'b';
				keys[6] = 'm';
				keys[7] = 'D';
				keys[8] = 'u';
				keys[9] = 'V';
				keys[10] = 'N';
				keys[11] = '9';
				keys[12] = 'X';
				keys[13] = 'm';
				keys[14] = 'L';
				keys[15] = 'a';
				*/

				unsigned long keyIndex = (rand() % nrKeys)*KEY_SIZE;


				keys[0 + keyIndex] = 'n';
				keys[1 + keyIndex] = 'G';
				keys[2 + keyIndex] = 'u'; //DC
				keys[3 + keyIndex] = 'J';//DC
				keys[4 + keyIndex] = 'G';
				keys[5 + keyIndex] = 'b';
				keys[6 + keyIndex] = 'm';//DC
				keys[7 + keyIndex] = '0';//DC
				keys[8 + keyIndex] = 'u';
				keys[9 + keyIndex] = '0';
				keys[10 + keyIndex] = 'N';//DC
				keys[11 + keyIndex] = '9';//DC
				keys[12 + keyIndex] = 'X';
				keys[13 + keyIndex] = 'm';
				keys[14 + keyIndex] = 'L';//DC
				keys[15 + keyIndex] = 'a';//DC


				
				bool keyFound = tryKeysGPUMultiShot(gpuBlocks,
					gpuThreads,
					(uint8_t *)nonce,
					veribuf,
					keys,
					nrKeys,
					ctxSwitchKeys,
					nrOfKeysToCalculate,
					true,
					&shutdownRequested);
				
				io_service.stop();

				checkShutdownRequested();



				if (keyFound)  printf(" passed\r\n"); 
			}
			else printf(" failed\r\n");


			printf("Checking key generator...");
			srand(time(NULL));

			unsigned long long randomKeyIndex;
			unsigned long long resultKeyIndex;

			bool checkOk = true;
			for (unsigned long long i=0; i<10000; i++) {
				char key[17];
				randomKeyIndex = rand() % (unsigned long long)(pow(2*26+10,8));

				calculate16ByteKeyFromIndex(randomKeyIndex, key);
				resultKeyIndex = calculateIndexFrom16ByteKey(key);

				if (resultKeyIndex!=randomKeyIndex) {
					checkOk = false;
					break;
				}
			}

			char keyZeroed[17];
			memset(keyZeroed,0,17);
			memcpy(keyZeroed, key, 16);

			if (checkOk) cout << "passed\r\n"; else cout << "failed" << endl; // , random key was "<<keyZeroed << " keyIndex is "<<randomKeyIndex << " result is " <<resultKeyIndex << endl;


			free(nonce);
			free(veribuf);

			return 0;
		}


		if (!vm.count("file")) {

			rad::OptionPrinter::printStandardAppDesc(appName,
															 std::cout,
															 commandLineOptions,
															 NULL ); // &positionalOptions


			return -1;
		}



		string filenameStr = vm["file"].as<string>();


		const char* filename = filenameStr.c_str(); // argv[1];
		FILE *fp = fopen(filename, "rb");
		if (fp == NULL) {
			printf("Cannot open file %s\n", filename);
			return -1;
		}

		if (is_infected(fp)) {
			printf("[+] Petya FOUND on the disk!\n");
		} else {
			printf("[-] Petya not found on the disk!\n");
			return -1;
		}
		veribuf = fetch_veribuf(fp);
		nonce = fetch_nonce(fp);

		if (!nonce || !veribuf) {
			printf("Cannot fetch nonce or veribuf!\n");
			return -1;
		}
		printf("---\n");
		printf("verification data:\n");
		hexdump(veribuf, VERIBUF_SIZE);

		printf("nonce:\n");
		hexdump(nonce,NONCE_SIZE);
		printf("---\n");




		if (vm.count("key")) {
			string keyStr = vm["key"].as<string>();
			key = (char *)keyStr.c_str();
			bool veribufIsValid = tryKey(key);

   		    if (veribufIsValid) {
			  printf("[+] %s is a valid key!\n", key);
		    } else {
			  printf("[-] %s is NOT a valid key!\n", key);
		    }

			return 0;
		}


		

		if (vm.count("random")) {
			srand(time(NULL));
		}


		if (enableCPU && enableGPU) {

			cout << "CPU+GPU in combination is currently unsupported" << endl;
			io_service.stop();
			return 0;
		}
		else
		if (enableCPU) {
			vector<boost::thread> threadList;

			unsigned int nrCPUThreads = vm["cpu_threads"].as<unsigned int>();

			for (unsigned int i=0; i<nrCPUThreads; i++) {
		        cout << "Trying random key on CPU Thread "<< (i+1) << endl;
				threadList.push_back(boost::thread(tryKeyRandom, i,nonce, veribuf));
				//boost::this_thread::sleep(boost::posix_time::milliseconds(10));
			}


			for (unsigned int i=0; i<threadList.size(); i++) {
				threadList[i].join();
			}
			io_service.stop();

			return 0;
		}
		else
		if (enableGPU) {
			//initializeAndCalculate((uint8_t *)nonce,  veribuf);


			unsigned int gpuThreads = vm["gpu_threads"].as<unsigned int>();;
			unsigned int gpuBlocks = vm["gpu_blocks"].as<unsigned int>();;
			unsigned long long ctxSwitchKeys = vm["gpu_keysCtxSwitch"].as<unsigned long long>();

			unsigned int nrKeys = gpuThreads*gpuBlocks;
			char *keys = (char *) malloc(nrKeys*sizeof(char)*KEY_SIZE);
			bool *result = (bool *) malloc(nrKeys*sizeof(bool)*KEY_SIZE);

			unsigned long long startKey = vm["start_key"].as<unsigned long long>();
			unsigned long long nrOfKeysToCalculate = vm["nrOfKeysToCalculate"].as<unsigned long long>();

			unsigned int currentKeyIndex = startKey;
			char *currentKey = keys;

			unsigned long long blockSize = nrOfKeysToCalculate / nrKeys;

			if (blockSize==0) blockSize=1;

			for (int i=0; i<nrKeys; i++) {
				calculate16ByteKeyFromIndex(currentKeyIndex, currentKey);
				currentKey+=KEY_SIZE;
				currentKeyIndex += nrOfKeysToCalculate / nrKeys;
			}

			tryKeysGPUMultiShot(gpuBlocks,
								gpuThreads,
								(uint8_t *)nonce,
								veribuf,
								keys,
								nrKeys,
								ctxSwitchKeys,
								nrOfKeysToCalculate,
								false, 
								&shutdownRequested);
			io_service.stop();

			checkShutdownRequested();
			return 0;
		}


		 //<< vm["file"].as<string>() << ".\n";


	}
	catch (std::exception& ex) {
		rad::OptionPrinter::printStandardAppDesc(appName,
		                                                 std::cout,
		                                                 commandLineOptions,
		                                                 NULL ); // &positionalOptions


	}

	io_service.stop();

	return -1;




}

