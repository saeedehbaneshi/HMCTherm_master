#include "Thermal.h"
#include <fstream>
#include <string>
#include <iostream>
#include <sstream> //stringstream
#include <stdlib.h> // getenv()
#include <math.h>




using namespace std; 
using namespace CasHMC; 

extern "C" double ***steady_thermal_solver(double ***powerM, double W, double Lc, int numP, int dimX, int dimZ, double **Midx, int count);
extern "C" double *transient_thermal_solver(double ***powerM, double W, double L, int numP, int dimX, int dimZ, double **Midx, int MidxSize, double *Cap, int CapSize, double time, int iter, double *T_trans);
extern "C" double **calculate_Midx_array(double W, double Lc, int numP, int dimX, int dimZ, int* MidxSize);
extern "C" double *calculate_Cap_array(double W, double Lc, int numP, int dimX, int dimZ, int* CapSize); 
extern "C" double *initialize_Temperature(double W, double Lc, int numP, int dimX, int dimZ); 
extern "C" double get_maxT(double *Tc, int Tsize);

extern string resultdir; // the directory name for storing the result
extern long PowerEpoch; 
// used for control the start point when the simulation is restarted
// clk_cycle_dist is the started clock cycle 
// cont_bool = 0 indicates starting from the very beginning (i.e. clk_cycle_dist = 0)
// cont_bool = 1 indicates starting from clk_cycle_dist --> all the previous data will be loaded
extern uint64_t clk_cycle_dist; 
extern int cont_bool; 
extern int num_refresh_save; 

ThermalCalculator::ThermalCalculator(bool withLogic_):
	totalEnergy(0.0),
	sampleEnergy(0.0),
	sample_id(0),
	withLogic(withLogic_)
	{
		num_refresh = num_refresh_save; 


		totRead_E = 0; totWrite_E = 0; totRef_E = 0; totACT_E = 0; totPre_E = 0; totBack_E = 0;
	    sapRead_E = 0; sapWrite_E = 0; sapRef_E = 0; sapACT_E = 0; sapPre_E = 0; sapBack_E = 0;

		power_epoch = PowerEpoch; 
		std::cout << "enter the assignment method\n";

		NUM_GRIDS_X = NUM_ROWS / MAT_X; 
		NUM_GRIDS_Y = NUM_COLS / MAT_Y; 


		int num_bank_per_layer = NUM_BANKS / NUM_LAYERS; 
		bank_y = num_bank_per_layer; 
		bank_x = 1; 
		vault_x = 1; 
		vault_y = NUM_VAULTS; 
		x = vault_x * bank_x * NUM_GRIDS_X; 
		y = vault_y * bank_y * NUM_GRIDS_Y;
		double asr = max(x, y) / min(x,y); 
		int vault_x_r = vault_x; 
		while (true){
			vault_x ++; 
			vault_y = NUM_VAULTS/vault_x; 
			if (vault_x * vault_y != NUM_VAULTS)
				continue; 
			x = vault_x * bank_x * NUM_GRIDS_X; 
			y = vault_y * bank_y * NUM_GRIDS_Y;
			double asr_n = max(x, y) / min(x,y);
			if (asr_n >= asr)
				break; 
			vault_x_r = vault_x; 
			asr = asr_n;  
		}
		vault_x = vault_x_r; 
		vault_y = NUM_VAULTS / vault_x; 


		x = vault_x * bank_x * NUM_GRIDS_X; 
		y = vault_y * bank_y * NUM_GRIDS_Y; 
		z = NUM_LAYERS;

		// initialize the accumulative power maps 
		accu_Pmap = vector<vector<vector<double> > > (x, vector<vector<double> > (y, vector<double> (z, 0)));
		accu_Pmap_wLogic = vector<vector<vector<double> > > (x, vector<vector<double> > (y, vector<double> (z+1, 0))); 
		// memory logic layer + processor layer

		cur_Pmap = vector<vector<vector<double> > > (x, vector<vector<double> > (y, vector<double> (z, 0)));
		cur_Pmap_wLogic = vector<vector<vector<double> > > (x, vector<vector<double> > (y, vector<double> (z+1, 0)));

		vault_usage_single = vector<int> (NUM_VAULTS, 0);
		vault_usage_multi = vector<int> (NUM_VAULTS, 0);

		std::cout << "(x, y, z) = " << "( " << x << ", " << y << ", " << z << " )" << std::endl; 

		std::cout << "vault_x = " << vault_x << "; vault_y = " << vault_y << std::endl;
		std::cout << "bank_x = " << bank_x << "; bank_y = " << bank_y << std::endl;

		///////// calculate Midx and MidxSize for temperature ///////////////
		calcMidx();
		

		/* define the file name */
		power_trace_str = resultdir + "power_trace.csv"; 
		temp_trace_str = resultdir + "temperature_trace.csv"; 
		power_stat_str = resultdir + "power_statics_trace.csv";
		avg_power_str = resultdir + "Average_Power_Profile.csv";
		final_temp_str = resultdir + "static_temperature.csv"; 
		dump_curCyc_str = resultdir + "currentClockCycle_file.txt";
		dump_Ttrans_str = resultdir + "Ttrans_file.txt"; 
		dump_accuP_str = resultdir + "accuP_file.txt";
		dump_curP_str = resultdir + "curP_file.txt";
		dump_Pstat_str = resultdir + "Pstat_file.txt";



		// if cont_bool == 1, reload the PTdata: T_trans, accu_Pmap, cur_Pmap
		if (cont_bool)
			Reload_PTdata(); 
		else{
			/* print the header for csv files */
			std::ofstream power_file; 
			std::ofstream temp_file; 
			std::ofstream power_stat_file; 
			power_file.open(power_trace_str.c_str()); power_file << "S_id,layer,x,y,power\n"; power_file.close();
			temp_file.open(temp_trace_str.c_str()); temp_file << "S_id,layer,x,y,temperature\n"; temp_file.close();
			power_stat_file.open(power_stat_str.c_str()); power_stat_file << "S_id,tot,Read,Write,ACT,Ref,Pre,Back\n"; power_stat_file.close();
		}

		cout << "sample_id = " << sample_id << endl;

		t = clock();
	}

ThermalCalculator::~ThermalCalculator()
{
	std::cout << "delete ThermalCalculator\n";
	Dump_PTdata(); 
	/* free the space of T */
    for (size_t i = 0; i < x; i++)
    {
        for (size_t j = 0; j < y; j++)
        {
            free(T_final[i][j]);
        }
        free(T_final[i]); 
    }
    free(T_final);

    for (int k = 0; k < MidxSize; k ++)
    {
        free(Midx[k]);
    }
    free(Midx); 

    free(T_trans); 
    free(Cap);
}

void ThermalCalculator::Dump_PTdata()
{
	std::ofstream Trans_file, accuP_file, curP_file, Pstat_file;  
	Trans_file.open(dump_Ttrans_str.c_str());
	accuP_file.open(dump_accuP_str.c_str());
	curP_file.open(dump_curP_str.c_str()); 
	Pstat_file.open(dump_Pstat_str.c_str());

	int numP = ( withLogic ? z+1 : z); 
	
	for (int i = 0; i < x * y * (numP*3+1); i++)
		Trans_file << T_trans[i] << " "; 
	Trans_file.close(); 

	for (int i = 0; i < x; i ++){
		for (int j = 0; j < y; j ++){
			for (int k = 0; k < z; k ++){
				accuP_file << accu_Pmap[i][j][k] << " "; 
				curP_file << cur_Pmap[i][j][k] << " "; 
			}
		}
	}
	accuP_file.close(); 
	curP_file.close();

	Pstat_file << sampleEnergy << "\n" << sapRead_E  << "\n" << sapWrite_E << "\n" << sapACT_E << "\n" << sapRef_E << "\n" << sapPre_E << "\n" << sapBack_E << std::endl;
	Pstat_file.close();
}

void ThermalCalculator::Reload_PTdata()
{
	// The file is the same as the dump file in Dump_PTdata
	std::ifstream Trans_file, accuP_file, curP_file, Pstat_file; 
	Trans_file.open(dump_Ttrans_str.c_str());
	accuP_file.open(dump_accuP_str.c_str());
	curP_file.open(dump_curP_str.c_str()); 
	Pstat_file.open(dump_Pstat_str.c_str());

	int numP = ( withLogic ? z+1 : z);

	for (int i = 0; i < x * y * (numP*3+1); i ++)
		Trans_file >> T_trans[i]; 
	Trans_file.close(); 

	for (int i = 0; i < x; i ++){
		for (int j = 0; j < y; j ++){
			for (int k = 0; k < z; k ++){
				accuP_file >> accu_Pmap[i][j][k]; 
				curP_file >> cur_Pmap[i][j][k]; 
			}
		}
	}
	accuP_file.close(); 
	curP_file.close();

	Pstat_file >> sampleEnergy; Pstat_file >> sapRead_E; Pstat_file >> sapWrite_E; Pstat_file >> sapACT_E; 
	Pstat_file >> sapRef_E; Pstat_file >> sapPre_E; Pstat_file >> sapBack_E;
	Pstat_file.close();
}


int ThermalCalculator::square_array(int total_grids_)
{
	int x, y, x_re = 1; 
	for (x = 1; x <= sqrt(total_grids_); x ++)
	{
		y = total_grids_ / x; 
		if (x * y == total_grids_)
			x_re = x; 
	}
	return x_re; 
}

void ThermalCalculator::addPower_refresh(double energy_t_, unsigned vault_id_, unsigned bank_id_, unsigned row_id_, unsigned col_id_, uint64_t cur_cycle)
{
	if (cont_bool){
		//cout << "hi refresh!\n";
	}

	if ((int)(cur_cycle/power_epoch) <= (int)(clk_cycle_dist/power_epoch) && cur_cycle <= clk_cycle_dist){
		if (cur_cycle > (sample_id+1) * power_epoch){
			//cout << "sample_id = " << sample_id << "; cur_cycle = " << cur_cycle << endl;
			sample_id = sample_id + 1;
		}
		return; 
	}
	//cout << "addPower_refresh\n";
	num_refresh ++;
	//cout << "num_refresh = " << num_refresh << endl;
	if (cur_cycle > (sample_id+1) * power_epoch)
	{
		//cout << "\ncur = " << cur_cycle << "; dist = " << clk_cycle_dist << "; sampleEnergy = " << sampleEnergy << "; sapRef_E = " << sapRef_E << "\n"; 
		save_sampleP(cur_cycle, sample_id); 
		cur_Pmap = vector<vector<vector<double> > > (x, vector<vector<double> > (y, vector<double> (z, 0)));
		sampleEnergy = 0; sapRead_E = 0; sapWrite_E = 0; sapRef_E = 0; sapACT_E = 0; sapPre_E = 0; sapBack_E = 0;
		sample_id = sample_id + 1; 
	}
	totalEnergy += energy_t_ * Vdd / 1000.0;
    sampleEnergy += energy_t_ * Vdd / 1000.0;
    totRef_E += energy_t_ * Vdd / 1000.0; 
    sapRef_E += energy_t_ * Vdd / 1000.0;
    //cout << "sapRef_E = " << sapRef_E << endl; 
    
    vault_usage_multi[vault_id_] ++; 
    double energy; 
    int ref_layer = 0, ref_row_phy = 0, ref_col_phy = 0; 

    energy = energy_t_ / (REFRESH_ROWNUM); 
    for (int i = 0; i < REFRESH_ROWNUM; i ++)
    {
    	mapPhysicalLocation(vault_id_, bank_id_, row_id_+i, col_id_, &ref_layer, &ref_row_phy, &ref_col_phy);
    	accu_Pmap[ref_row_phy][ref_col_phy][ref_layer] += energy * Vdd / 1000.0;
    	cur_Pmap[ref_row_phy][ref_col_phy][ref_layer] += energy * Vdd / 1000.0;
    }

}


void ThermalCalculator::addPower(double energy_t_, unsigned vault_id_, unsigned bank_id_, unsigned row_id_, unsigned col_id_, bool single_bank, uint64_t cur_cycle, int cmd_type)
{

    if ((int)(cur_cycle/power_epoch) <= (int)(clk_cycle_dist/power_epoch)  && cur_cycle <= clk_cycle_dist){
		if (cur_cycle > (sample_id+1) * power_epoch){
			sample_id = sample_id + 1;
		}
		return; 
	}

	////// determine whether the sampling period ends //////////////
	if (cur_cycle > (sample_id+1) * power_epoch)
	{
		save_sampleP(cur_cycle, sample_id); 
		cur_Pmap = vector<vector<vector<double> > > (x, vector<vector<double> > (y, vector<double> (z, 0)));
		sampleEnergy = 0; sapRead_E = 0; sapWrite_E = 0; sapRef_E = 0; sapACT_E = 0; sapPre_E = 0; sapBack_E = 0;
		sample_id = sample_id + 1; 
	}

	////////////////////////////////////////////////////////////////


    totalEnergy += energy_t_ * Vdd / 1000.0;
    sampleEnergy += energy_t_ * Vdd / 1000.0;
    switch (cmd_type){
    	case 0:
    		totBack_E += energy_t_ * Vdd / 1000.0;
    		sapBack_E += energy_t_ * Vdd / 1000.0;
    		break;
    	case 1: 
    		totACT_E += energy_t_ * Vdd / 1000.0;
    		sapACT_E += energy_t_ * Vdd / 1000.0;
    		break;
    	case 2: 
    		totRead_E += energy_t_ * Vdd / 1000.0;
    		sapRead_E += energy_t_ * Vdd / 1000.0;
    		break;
    	case 3: 
    		totWrite_E += energy_t_ * Vdd / 1000.0;
    		sapWrite_E += energy_t_ * Vdd / 1000.0;
    		break;
    	case 4: 
    		totPre_E += energy_t_ * Vdd / 1000.0;
    		sapPre_E += energy_t_ * Vdd / 1000.0;
    		break;
    }
    
	if (single_bank)
	{
		vault_usage_single[vault_id_] ++;
		int layer = 0, row_phy = 0, col_phy = 0; 
		mapPhysicalLocation(vault_id_, bank_id_, row_id_, col_id_, &layer, &row_phy, &col_phy);
		accu_Pmap[row_phy][col_phy][layer] += energy_t_ * Vdd / 1000.0; 
		cur_Pmap[row_phy][col_phy][layer] += energy_t_ * Vdd / 1000.0; 

	}
	else
	{
		vault_usage_multi[vault_id_] ++;
		double energy; 
		int base_layer = 0, base_row_phy = 0, base_col_phy = 0; 
		mapPhysicalLocation(vault_id_, bank_id_, row_id_, col_id_, &base_layer, &base_row_phy, &base_col_phy);
		

        energy = energy_t_ / (NUM_GRIDS_X * NUM_GRIDS_Y); 
		for (int i = base_row_phy; i < base_row_phy + NUM_GRIDS_X; i ++)
		{
			for (int j = base_col_phy; j < base_col_phy + NUM_GRIDS_Y; j ++)
			{
				accu_Pmap[i][j][base_layer] += energy * Vdd / 1000.0; 
				cur_Pmap[i][j][base_layer] += energy * Vdd / 1000.0; 
			}
		}
        	

	}

}


void ThermalCalculator::rev_mapPhysicalLocation(int *vault_id_, int *bank_id_, int *row_s, int *row_e, int layer, int row, int col)
{

	int grid_step = NUM_ROWS / (NUM_GRIDS_X * NUM_GRIDS_Y);
	int num_bank_per_layer = NUM_BANKS / NUM_LAYERS;  
	int bx = row / NUM_GRIDS_X; 
	int by = col / NUM_GRIDS_Y; 
	int vx = bx / bank_x; 
	int vy = by / bank_y; 
	// get the vault id
	*vault_id_ = vx * vault_y + vy; 

	// get the bank id
	int bank_same_layer = (bx % bank_x)* bank_y + (by % bank_y); 
	*bank_id_ = layer * num_bank_per_layer + bank_same_layer; 

	// get the row id
	int grid_id = (row % NUM_GRIDS_X) * NUM_GRIDS_Y + (col % NUM_GRIDS_Y); 
	*row_s = grid_id * grid_step; 
	*row_e = (grid_id+1) * grid_step; 
}



void ThermalCalculator::mapPhysicalLocation(unsigned vault_id_, unsigned bank_id_, unsigned row_id_, unsigned col_id_, int *layer, int *row, int *col)
{

	int vault_id_x = vault_id_ / vault_y; 
	int vault_id_y = vault_id_ % vault_y; 

	// layer # is determined by the index of bank
	// each bank is divided into NUM_GRIDS_X * NUM_GRIDS_Y thermal grids 
	// all the thermal grids within one bank lie on the same layer
	int num_bank_per_layer = NUM_BANKS / NUM_LAYERS; 
	*layer = bank_id_ / num_bank_per_layer; 

	int bank_same_layer = bank_id_ % num_bank_per_layer; 
	int bank_id_x = bank_same_layer / bank_y; 
	int bank_id_y = bank_same_layer % bank_y; 

	int grid_step = NUM_ROWS / (NUM_GRIDS_X * NUM_GRIDS_Y); 
	int grid_id = row_id_ / grid_step; 
	int grid_id_x = grid_id / NUM_GRIDS_Y; 
	int grid_id_y = grid_id % NUM_GRIDS_Y; 

    *row = vault_id_x * (bank_x * NUM_GRIDS_X) + bank_id_x * NUM_GRIDS_X + grid_id_x; 
    *col = vault_id_y * (bank_y * NUM_GRIDS_Y) + bank_id_y * NUM_GRIDS_Y + grid_id_y;

}

void ThermalCalculator::printP_new(uint64_t cur_cycle){
	genTotalP(true, cur_cycle);
	uint64_t ElapsedCycle = cur_cycle; 
	std::ofstream power_file; 
	power_file.open(avg_power_str.c_str()); 
	power_file << "layer_type,z,x,y,power,vault,bank\n";
	for (int iz = 0; iz < z; iz ++){
		for (int iy = 0; iy < y; iy ++){
			for (int ix = 0; ix < x; ix ++){
				power_file << "MEM," << iz << "," << ix << "," << iy << "," << accu_Pmap_wLogic[ix][iy][iz] / (double) ElapsedCycle << "," << ix/(x/vault_x)*vault_y + iy/(y/vault_y) << "," << (ix%(x/vault_x))/NUM_GRIDS_X*bank_y + (iy%(y/vault_y))/NUM_GRIDS_Y  <<std::endl;
			}
		}
	}
	for (int iy = 0; iy < y; iy ++){
		for (int ix = 0; ix < x; ix ++){
			power_file << "LOGIC," << z << "," << ix << "," << iy << "," << accu_Pmap_wLogic[ix][iy][z] / (double) ElapsedCycle << ",-1,-1," << std::endl;
		}
	}

	power_file.close();
}


void ThermalCalculator::printTtrans(unsigned S_id)
{
	// extract the temperature to a 3D array 
	int numP, dimX, dimZ; 

	dimX = x; dimZ = y; 
	numP = (withLogic ? z+1 : z);

	double maxTT; maxTT = get_maxT(T_trans, dimX*dimZ*(numP*3+1));
    std::cout << "Now maxT = " << maxTT - T0 << std::endl;

	vector<vector<vector<double> > > T;
	vector<int> layerP; 
	T = vector<vector<vector<double> > > (dimX, vector<vector<double> > (dimZ, vector<double> (numP, 0)));
	layerP = vector<int> (numP, 0);


	for (int l = 0; l < numP; l ++){
		layerP[l] = l * 3;
        for (int i = 0; i < dimX; i ++){
            for (int j = 0; j < dimZ; j++){
                T[i][j][l] = T_trans[dimX*dimZ*(layerP[l]+1) + i*dimZ + j];
            }
        }
	}

	/////////////// print out to files ///////////////
	std::ofstream temp_file; 
	temp_file.open(temp_trace_str.c_str(), std::ios_base::app); 
	for (int iz = 0; iz < numP; iz ++){
		for (int iy = 0; iy < dimZ; iy ++){
			for (int ix = 0; ix < dimX; ix ++){
				temp_file << S_id << "," << iz << "," << ix << "," << iy << "," << T[ix][iy][iz] - T0 << std::endl;
			}
		}
	}
	temp_file.close(); 

}

void ThermalCalculator::printSamplePower(uint64_t cur_cycle, unsigned S_id){
	uint64_t ElapsedCycle = cur_cycle; 
	std::ofstream power_file; 
	power_file.open(power_trace_str.c_str(), std::ios_base::app);
	if (withLogic){
		for (int iz = 0; iz < z+1; iz ++){
			for (int iy = 0; iy < y; iy ++){
				for (int ix = 0; ix < x; ix ++){
					power_file << S_id << "," << iz << "," << ix << "," << iy << "," << cur_Pmap_wLogic[ix][iy][iz] / (double) ElapsedCycle << std::endl;
				}
			}
		}
		power_file.close(); 
	}
	else{
		for (int iz = 0; iz < z; iz ++){
			for (int iy = 0; iy < y; iy ++){
				for (int ix = 0; ix < x; ix ++){
					power_file << S_id << "," << iz << "," << ix << "," << iy << "," << cur_Pmap_wLogic[ix][iy][iz] / (double) ElapsedCycle << std::endl;
				}
			}
		}
		power_file.close();
	}


	// print the power statics
	std::cout << "sapRef_E = " << sapRef_E << "; ElapsedCycle = " << ElapsedCycle << std::endl;
	std::ofstream power_stat_file; 
	power_stat_file.open(power_stat_str.c_str(), std::ios_base::app); 
	power_stat_file << S_id << "," << sampleEnergy /(double) ElapsedCycle << "," << sapRead_E /(double) ElapsedCycle << "," 
	                << sapWrite_E /(double) ElapsedCycle << "," << sapACT_E /(double) ElapsedCycle << ","
	                << sapRef_E /(double) ElapsedCycle << "," << sapPre_E /(double) ElapsedCycle << ","
	                << sapBack_E /(double) ElapsedCycle << std::endl;
	power_stat_file.close();

}


void ThermalCalculator::printT()
{
	// print out the temperature profile calcualted using the accumulated power 
	std::ofstream temp_file; 
	temp_file.open(final_temp_str.c_str()); 
	temp_file << "layer,x,y,temperature\n"; 

	int numlayer = (withLogic ? z+1 : z); 

	for (int iz = 0; iz < numlayer; iz ++){
		for (int iy = 0; iy < y; iy ++){
			for (int ix = 0; ix < x; ix ++){
				temp_file << iz << "," << ix << "," << iy << "," << T_final[ix][iy][iz] << std::endl;
			}
		}
	}
	temp_file.close();
}


void ThermalCalculator::printVaultUsage()
{
	std::cout << "Single cell usage:\n";
	for (int i = 0; i < NUM_VAULTS; i++)
		std::cout << vault_usage_single[i] << ", "; 
	std::cout << std::endl;

	std::cout << "Multi cell usage:\n";
	for (int i = 0; i < NUM_VAULTS; i++)
		std::cout << vault_usage_multi[i] << ", "; 
	std::cout << std::endl;
}

void ThermalCalculator::genTotalP(bool accuP, uint64_t cur_cycle)
{
	/* accuP = true: calculate for the accumulative power */
	/* accuP = false: calculate for the transient power */

	double logicE, cellE_ratio, cellE; 
	uint64_t ElapsedCycle = cur_cycle; 
	std::vector<std::vector<std::vector<double> > > origP; 
	std::vector<std::vector<std::vector<double> > > newP = accu_Pmap_wLogic; // just initilize it 


	double val = 0.0;

	if (accuP)
	{
		// calculate the accumulative P
		origP = accu_Pmap; 
		logicE = totalEnergy * 1.83; 
		// the ratio is from "Data compression for thermal mitigation in HMC"
	}
	else
	{
		origP = cur_Pmap; 
		logicE = sampleEnergy * 1.83;
	}
	cellE = logicE / x / y; 

	for (int l = 0; l < z+1; l ++)
	{
		if (l == z)
		{
			for (int i = 0; i < x; i ++)
				for (int j = 0; j < y; j ++)
					newP[i][j][l] = cellE; 
		}
		else
		{
			for (int i = 0; i < x; i ++)
				for (int j = 0; j < y; j ++)
					newP[i][j][l] = origP[i][j][l];
		}
	}

	if (accuP)
		accu_Pmap_wLogic = newP; 
	else
		cur_Pmap_wLogic = newP; 
}


void ThermalCalculator::calcT(uint64_t cur_cycle)
{
	// withLogic = 1: calculate with logic layer 
	// withLogic = 0: calculate only the DRAM layers 
	double ***powerM; 
	int i, j, l; // indicators 
	int numP, dimX, dimZ; 
	// uint64_t ElapsedCycle = (cur_cycle % LOG_EPOCH == 0)?(LOG_EPOCH):(cur_cycle % LOG_EPOCH); 
	uint64_t ElapsedCycle = cur_cycle; 

	dimX = x; dimZ = y; 
	if (withLogic){
		numP = z+1; 
		genTotalP(true, cur_cycle);
	}
	else{
		numP = z; 
	}

	if ( !(powerM = (double ***)malloc(dimX * sizeof(double **))) ) printf("Malloc fails for powerM[].\n");
    for (i = 0; i < dimX; i++)
    {
        if ( !(powerM[i] = (double **)malloc(dimZ * sizeof(double *))) ) printf("Malloc fails for powerM[%d][].\n", i);
        for (j = 0; j < dimZ; j++)
        {
            if ( !(powerM[i][j] = (double *)malloc(numP * sizeof(double))) ) printf("Malloc fails for powerM[%d][%d][].\n", i,j);
        }
    }

    if (withLogic)
    {
    	for (i = 0; i < dimX; i ++)
    		for (j = 0; j < dimZ; j ++)
    			for (l = 0; l < numP; l ++)
    				powerM[i][j][l] = accu_Pmap_wLogic[i][j][l] / (double) ElapsedCycle; 
    }
    else
    {
    	for (i = 0; i < dimX; i ++)
    		for (j = 0; j < dimZ; j ++)
    			for (l = 0; l < numP; l ++)
    				powerM[i][j][l] = accu_Pmap[i][j][l] / (double) ElapsedCycle; 
    }


    double sum_power = 0.0; 
    for (i = 0; i < dimX; i ++){
    	for (j = 0; j < dimZ; j ++){
    		for (l = 0; l < numP; l ++){
    			sum_power += powerM[i][j][l];
    		}
    	}
    }
    cout << "sum_power = " << sum_power << endl;


    T_final = steady_thermal_solver(powerM, ChipX, ChipZ, numP, dimX, dimZ, Midx, MidxSize);

    // print the final temperature profile 
    printT();

}


void ThermalCalculator::save_sampleP(uint64_t cur_cycle, unsigned S_id)
{
	/* In this method, we save the sampling power to the file */
	/* We also calculate the transient temperature at this time point */
	/* The current transient temperature is stored in T_trans */
	/* Also, the current transient temperature is written to the file */

	genTotalP(false, power_epoch); 
	printSamplePower(power_epoch, S_id); 

	cout << "\ncur = " << cur_cycle << "; dist = " << clk_cycle_dist << "; sampleEnergy = " << sampleEnergy << "; sapRef_E = " << sapRef_E << "\n"; 
	cout << "time = " << float(clock() - t)/CLOCKS_PER_SEC << " [s]\n";  
	cout << "========= solve for Sample " << S_id << "==== Current time is " << power_epoch * (S_id+1) * tCK * 1e-9 << "[s] ================\n";

	t = clock(); 
	//bool withLogic = true;

	////// calclate the transient temperature ////////
	calc_trans_T();

	printTtrans(S_id);

	/////////////// Update the Dynamic Management Information ///////////////
	cout << "num_refresh = " << num_refresh << endl;



}


void ThermalCalculator::calc_trans_T()
{
	double time = power_epoch * tCK * 1e-9; // [s]
	// withLogic = 1: calculate with logic layer 
	// withLogic = 0: calculate only the DRAM layers 
	double ***powerM; 
	int i, j, l; // indicators 
	int numP, dimX, dimZ; 
	// uint64_t ElapsedCycle = (cur_cycle % LOG_EPOCH == 0)?(LOG_EPOCH):(cur_cycle % LOG_EPOCH); 
	uint64_t ElapsedCycle = power_epoch;

	dimX = x; dimZ = y; 
	numP = (withLogic ? z+1 : z);


	if ( !(powerM = (double ***)malloc(dimX * sizeof(double **))) ) printf("Malloc fails for powerM[].\n");
    for (i = 0; i < dimX; i++)
    {
        if ( !(powerM[i] = (double **)malloc(dimZ * sizeof(double *))) ) printf("Malloc fails for powerM[%d][].\n", i);
        for (j = 0; j < dimZ; j++)
        {
            if ( !(powerM[i][j] = (double *)malloc(numP * sizeof(double))) ) printf("Malloc fails for powerM[%d][%d][].\n", i,j);
        }
    }

    if (withLogic)
    {
    	for (i = 0; i < dimX; i ++)
    		for (j = 0; j < dimZ; j ++)
    			for (l = 0; l < numP; l ++)
    				powerM[i][j][l] = cur_Pmap_wLogic[i][j][l] / (double) ElapsedCycle; 
    }
    else
    {
    	for (i = 0; i < dimX; i ++)
    		for (j = 0; j < dimZ; j ++)
    			for (l = 0; l < numP; l ++)
    				powerM[i][j][l] = cur_Pmap[i][j][l] / (double) ElapsedCycle; 
    }


    int time_iter = TimeIter0; 
    while (time / time_iter >= max_tp)
    	time_iter ++; 


    T_trans = transient_thermal_solver(powerM, ChipX, ChipZ, numP, dimX, dimZ, Midx, MidxSize, Cap, CapSize, time, time_iter, T_trans);


}

void ThermalCalculator::calcMidx()
{
	int dimX, dimZ, numP;

	dimX = x; dimZ = y; 
	numP = (withLogic ? z+1 : z);


	layerP_ = vector<int> (numP, 0);
	for(int l = 0; l < numP; l++)
        layerP_[l] = l * 3;



	Midx = calculate_Midx_array(ChipX, ChipZ, numP, dimX, dimZ, &MidxSize);
	Cap = calculate_Cap_array(ChipX, ChipZ, numP, dimX, dimZ, &CapSize);
	calculate_time_step();
	T_trans = initialize_Temperature(ChipX, ChipZ, numP, dimX, dimZ);
}

void ThermalCalculator::calculate_time_step()
{
	double dt = 100.0; 
	int layer_dim = x * y; 
	double c, g; 

	for (int l = 0; l < CapSize; l ++)
	{
		c = Cap[l]; 
		for (int i = 0; i < layer_dim; i ++)
		{
			if (Midx[l*layer_dim + i][0] == Midx[l*layer_dim + i][1])
			{
				g = Midx[l*layer_dim + i][2]; 
				if (c/g < dt) 
					dt = c/g; 
			}
		}
	}

	cout << "maximum dt is " << dt << endl;
	max_tp = dt; 

}


int ThermalCalculator::get_x()
{
	return x; 
}
int ThermalCalculator::get_y()
{
	return y; 
}
int ThermalCalculator::get_z()
{
	return z; 
}
double ThermalCalculator::get_totalE()
{
	std::cout << "Total Energy = " << totalEnergy << std::endl;
	return totalEnergy;
}
double ThermalCalculator::get_IOE()
{
	return IOEnergy;
}

void ThermalCalculator::printRT(uint64_t cur_cycle)
{
	std::ofstream cur_file; 

	cur_file.open(dump_curCyc_str.c_str());
	cur_file << cur_cycle << endl;
	cur_file << num_refresh << endl;
	cur_file.close(); 

}