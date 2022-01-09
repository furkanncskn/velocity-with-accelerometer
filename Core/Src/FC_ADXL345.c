#include "FC_ADXL345.h"
#include "FC_UTILITY.h"

#include <stdint.h>
#include <stdlib.h>

/*! Macros Declarations */

#define 	PUBLIC
#define 	PRIVATE 							static

#define   ADXL345_I2C_ADRESS    (0x53 << 1)                  // ADXL345 device address
#define   ADXL345_WHO_AM_I			(0x00)               	       // a fixed device ID code of 0xE5
#define   ADXL345_FIXED_VAL			(0xE5)
#define   ADXL345_DATA_FORMAT   (0x31)
#define   ADXL345_POWER_CTL     (0x2D)
#define   ADXL345_DATAX0        (0x32)
#define		CALIBRATION_SIZE			(1024)
#define 	ACCEL_LOWER_LIMIT			(-1)
#define 	ACCEL_UPPER_LIMIT			(+1)
#define 	MILIS_TO_SECOND				(0.001)


/*!< Typedef Declarations */

typedef enum {

	ADXL345_2G_RANGE_SEN = 256,
	ADXL345_4G_RANGE_SEN = 128,
	ADXL345_8G_RANGE_SEN = 64,
	ADXL345_16G_RANGE_SEN = 32,

}ADXL345SensivityRange;


/*!< Static Function Declarations */

PRIVATE uint8_t ADXL345_Reset(I2C_HandleTypeDef* i2cHandle);
PRIVATE uint8_t ADXL345_SelectSensivityRange(I2C_HandleTypeDef* i2cHandle, ADXL345* adxl, uint8_t range);


/*!
 * @bried ADXL345 nesnesi hayata getirr
 *
 * return
 * 		  Hayata getirilen nesnenin adresi
 */
PUBLIC ADXL345* ADXL345_CreateObject(void) {
	ADXL345* adxl = (ADXL345*)malloc(sizeof(ADXL345));
	if (!adxl)
		return NULL;

	return adxl;
}


/*!
 * @bried ADXL345 nesnesinin hayatini sonlandirir
 *
 */
PUBLIC void ADXL345_DeleteObject(ADXL345* adxl) {
	if (!adxl)
		return;

	free(adxl);
}


/*!
 *  @brief  Sensorun varliginin kontrol eder.
 *
 *  @return
 *	    	Cihaz var ise, '0',
 *	    	Cihaz yok ise  '1'
 */
PUBLIC uint8_t ADXL345_IsWhoAmI(I2C_HandleTypeDef* i2cHandle) {
	uint8_t data = 0;
	if (I2C_Read(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_WHO_AM_I, 1, &data))
		if (data == ADXL345_FIXED_VAL)
			return TRUE;

	return FALSE;
}


/*
 * @brief
 * 			Sensor kullanima hazir hale getirilir
 *
 * @return
 * 			Basarisizlik durumunda 	'1'
 * 			Basari durumunda 		'0'
 */
PUBLIC uint8_t ADXL345_Init(I2C_HandleTypeDef* i2cHandle, ADXL345* adxl, uint8_t range) {
	if (ADXL345_Reset(i2cHandle)) // reset all bits
		return FCFAIL;

	uint8_t val = 0x08; // measure and wake up
	if (I2C_Write(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_POWER_CTL, &val) == FCFAIL)
		return FCFAIL;

	if (ADXL345_SelectSensivityRange(i2cHandle, adxl, range))
		return FCFAIL;

	return SUCCESS;
}


/*!
 *
 * @brief
 * 			Ham degerleri sensorden okur
 *
 * @return
 * 			Basari durumunda '0'
 * 			Basarisizlik durumunda '1'
 *
 */
PUBLIC uint8_t ADXL345_ReadRawValueFromAccel(I2C_HandleTypeDef* i2cHandle, ADXL345* adxl) {
	uint8_t data[6] = { 0 };

	HAL_I2C_Mem_Read(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_DATAX0, 1, data, 6, 100);

	adxl->accRaw[0] = (int16_t)((data[0]) | (data[1] << 8));
	adxl->accRaw[1] = (int16_t)((data[2]) | (data[3] << 8));
	adxl->accRaw[2] = (int16_t)((data[4]) | (data[5] << 8));

	return FCSUCCESS;
}


/**
 * @brief Başlangıç offset değerlerini hesaplar kalibrasyon yapar
 *
 */
PUBLIC void ADXL345_SetOffsetValues(I2C_HandleTypeDef* i2cHandle, ADXL345* adxl) {
	double temp_buffer[3] = { 0, 0, 0 };
	int cnt;
	for (cnt = 0; cnt < CALIBRATION_SIZE; ) {
		if (!ADXL345_ReadRawValueFromAccel(i2cHandle, adxl)) {
			temp_buffer[0] += adxl->accRaw[0];
			temp_buffer[1] += adxl->accRaw[1];
			temp_buffer[2] += adxl->accRaw[2];
			++cnt;
			HAL_Delay(5);
		}
	}
	adxl->accOffset[0] = temp_buffer[0] / cnt;
	adxl->accOffset[1] = temp_buffer[1] / cnt;
	adxl->accOffset[2] = 255. - temp_buffer[2] / cnt;
}


/**
 * @brief
 * 			Ham değerde ki kaymayı telafi edebilmek için cal değerinden çıkartılarak
 * 			(LSB - LSB) / (LSB/g) işlemi ile g türüne çevirildi ve ivme değeri elde
 * 			edildi.
 */
PUBLIC void ADXL345_SetAccelerations(I2C_HandleTypeDef* i2cHandle, ADXL345* adxl) {
	for (int i = 0; i < 3; ++i) {
		adxl->accVal[i] = (adxl->accRaw[i] - adxl->accOffset[i]) / adxl->sensivity * EARTH_GRAVITY;
	}
}


/**
 * @brief
 * 			İvmeölçer sensörümüz darbeye bağlı anlık değişimlerden etkilenir. Bu sebeple anlık darbe
 * 			etkisini minimuma indirmek için moving avarage filter adında ki belirli sayıda
 * 			örneğin ortalamasını alan filtre kullanıldı.
 */
PUBLIC void ADXL345_FIRAvarageFilter(I2C_HandleTypeDef* i2cHandle, ADXL345* adxl, size_t FIRfilter_size) {

	double tempAccelSumVal[3] = { 0, 0, 0 };

	int cnt;
	for (cnt = 0; cnt < FIRfilter_size; ) {
		if (!ADXL345_ReadRawValueFromAccel(i2cHandle, adxl)) {

			ADXL345_SetAccelerations(i2cHandle, adxl);

			for (int i = 0; i < 3; ++i)
				tempAccelSumVal[i] += adxl->accVal[i];

			++cnt;
		}
	}

	for (int i = 0; i < 3; ++i) {
		adxl->FIRfilterAcc[i] = tempAccelSumVal[i] / cnt;
	}
}


/**
 * @brief
 *
 * 			Titreşimden veya farklı etkilerden kaynaklanan gürültünün
 * 			azaltmak için IIR filtre tasarımı.
 */
PUBLIC void ADXL345_IIRLowPassFilter(ADXL345* adxl, double IIRfilter_rate) {

	static double previous_filter_values[3] = { 0 };

	for (int i = 0; i < 3; ++i) {

		// Reset the acceleration in the range
		if ((int)adxl->FIRfilterAcc[i] > ACCEL_LOWER_LIMIT && (int)adxl->FIRfilterAcc[i] < ACCEL_UPPER_LIMIT) {
			adxl->FIRfilterAcc[i] = 0;
			previous_filter_values[i] = 0;
		}

		// IIR filter
		adxl->IIRfilterAcc[i] = adxl->FIRfilterAcc[i] * (1 - IIRfilter_rate) + previous_filter_values[i] * IIRfilter_rate;
		previous_filter_values[i] = adxl->IIRfilterAcc[i];
		adxl->accRecent[i] = adxl->IIRfilterAcc[i];
	}
}


/**
 * @brief
 *
 * 			İvme değerinin integarali alınarak hiz
 * 			degeri hesaplanir.
 *
 */
void ADXL345_SetVelocity(ADXL345* adxl, double deltaT) {
	static double previous_velocity[3] = { 0 };
	static int16_t cnt = 0;

	for (int i = 0; i < 3; ++i) {

		/* if the acceleration is zero for a while, reset speed and previous accel */
		if ((int)adxl->accRecent[i] == 0 && (int)adxl->accRecent[i] == 0) {
			cnt++;
			if (cnt > 32) {
				cnt = 0;
				adxl->accVelocity[i] = 0;
				previous_velocity[i] = 0;
				adxl->accPrevious[i] = 0;
			}
		}

		/* Calculate velocity, integral of the acceleration */
		adxl->accVelocity[i] = previous_velocity[i] + (adxl->accPrevious[i] + ((adxl->accRecent[i] - adxl->accPrevious[i]) / 2)) * (deltaT * MILIS_TO_SECOND);
		previous_velocity[i] = adxl->accVelocity[i];
		adxl->accPrevious[i] = adxl->accRecent[i];
	}
}


/**
 * @brief
 * 			Sensore reser atar.
 *
 * @return
 * 			Basari durumunda '0'
 * 			Basarisizlik durumunda '1'
 */
PRIVATE uint8_t ADXL345_Reset(I2C_HandleTypeDef* i2cHandle) {
	if (I2C_Write(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_POWER_CTL, 0x00))
		return FCFAIL;

	return FCSUCCESS;
}


/**
 * @brief
 * 			Sensörün hassasiyetlik ayarini secer
 *
 * @return
 * 			Basari durumunda '0'
 * 			Basarisizlik durumunda '1'
 */
PRIVATE uint8_t ADXL345_SelectSensivityRange(I2C_HandleTypeDef* i2cHandle, ADXL345* adxl, uint8_t range) {
	uint8_t val;
	switch (range) {
	case ADXL345_2G:
		val = 0x00;
		if (I2C_Write(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_DATA_FORMAT, &val) == FCFAIL)
			return FCFAIL;

		adxl->sensivity = ADXL345_2G_RANGE_SEN;
		break;
	case ADXL345_4G:
		val = 0x01;
		if (I2C_Write(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_DATA_FORMAT, &val) == FCFAIL)
			return FCFAIL;

		adxl->sensivity = ADXL345_4G_RANGE_SEN;
		break;
	case ADXL345_8G:
		val = 0x10;
		if (I2C_Write(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_DATA_FORMAT, &val) == FCFAIL)
			return FCFAIL;

		adxl->sensivity = ADXL345_8G_RANGE_SEN;
		break;
	case ADXL345_16G:
		val = 0x11;
		if (I2C_Write(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_DATA_FORMAT, &val) == FCFAIL)
			return FCFAIL;

		adxl->sensivity = ADXL345_16G_RANGE_SEN;
		break;
	default:
		val = 0x00;
		if (I2C_Write(i2cHandle, ADXL345_I2C_ADRESS, ADXL345_DATA_FORMAT, &val) == FCFAIL)
			return FCFAIL;

		adxl->sensivity = ADXL345_2G_RANGE_SEN;
		break;
	}

	return FCSUCCESS;
}


PUBLIC int16_t ADXL345_GetRawXValue(const ADXL345* adxl) {
	return adxl->accRaw[0];
}

PUBLIC int16_t ADXL345_GetRawYValue(const ADXL345* adxl) {
	return adxl->accRaw[1];
}

PUBLIC int16_t ADXL345_GetRawZValue(const ADXL345* adxl) {
	return adxl->accRaw[2];
}

PUBLIC double ADXL345_GetAccelerationX(const ADXL345* adxl) {
	return adxl->accVal[0];
}

PUBLIC double ADXL345_GetAccelerationY(const ADXL345* adxl) {
	return adxl->accVal[1];
}

PUBLIC double ADXL345_GetAccelerationZ(const ADXL345* adxl) {
	return adxl->accVal[2];
}

double ADXL345_GetFIRFilterAccelerationX(const ADXL345* adxl) {
	return adxl->FIRfilterAcc[0];
}

double ADXL345_GetFIRFilterAccelerationY(const ADXL345* adxl) {
	return adxl->FIRfilterAcc[1];
}

double ADXL345_GetFIRFilterAccelerationZ(const ADXL345* adxl) {
	return adxl->FIRfilterAcc[2];
}

PUBLIC double ADXL345_GetIIRFilterAccelerationX(const ADXL345* adxl) {
	return adxl->IIRfilterAcc[0];
}

PUBLIC double ADXL345_GetIIRFilterAccelerationY(const ADXL345* adxl) {
	return adxl->IIRfilterAcc[1];
}

PUBLIC double ADXL345_GetIIRFilterAccelerationZ(const ADXL345* adxl) {
	return adxl->IIRfilterAcc[2];
}

PUBLIC double ADXL345_GetVelocityX(const ADXL345* adxl) {
	return adxl->accVelocity[0];
}

PUBLIC double ADXL345_GetVelocityY(const ADXL345* adxl) {
	return adxl->accVelocity[1];
}

PUBLIC double ADXL345_GetVelocityZ(const ADXL345* adxl) {
	return adxl->accVelocity[2];
}