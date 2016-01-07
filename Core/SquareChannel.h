#pragma once
#include "stdafx.h"
#include "../BlipBuffer/Blip_Buffer.h"
#include "APU.h"
#include "IMemoryHandler.h"
#include "ApuEnvelope.h"

class SquareChannel : public ApuEnvelope
{
private:
	const vector<vector<uint8_t>> _dutySequences = { {
		{ 0, 1, 0, 0, 0, 0, 0, 0 },
		{ 0, 1, 1, 0, 0, 0, 0, 0 },
		{ 0, 1, 1, 1, 1, 0, 0, 0 },
		{ 1, 0, 0, 1, 1, 1, 1, 1 }
	} };

	bool _isChannel1 = false;

	uint8_t _duty = 0;
	uint8_t _dutyPos = 0;
	
	bool _sweepEnabled = false;
	uint8_t _sweepPeriod = 0;
	bool _sweepNegate = false;
	uint8_t _sweepShift = 0;
	bool _reloadSweep = false;
	uint8_t _sweepDivider = 0;
	uint32_t _sweepTargetPeriod = 0;
	
	bool IsMuted()
	{
		//A period of t < 8, either set explicitly or via a sweep period update, silences the corresponding pulse channel.
		return _period < 8 || _sweepTargetPeriod > 0x7FF;
	}

	void InitializeSweep(uint8_t regValue)
	{
		_sweepEnabled = (regValue & 0x80) == 0x80;
		_sweepNegate = (regValue & 0x08) == 0x08;

		//The divider's period is set to P + 1 
		_sweepPeriod = ((regValue & 0x70) >> 4) + 1;
		_sweepShift = (regValue & 0x07);

		//Side effects: Sets the reload flag 
		_reloadSweep = true;
	}

	void UpdateTargetPeriod(bool setPeriod)
	{
		uint16_t shiftResult = (_period >> _sweepShift);
		if(_sweepNegate) {
			_sweepTargetPeriod = _period - shiftResult;
			if(_isChannel1) {
				//"As a result, a negative sweep on pulse channel 1 will subtract the shifted period value minus 1"
				//Turns out this line is a bit confusing - channel 1 is meant to do "_period - (shiftResult + 1)", so we need to subtract 1 in this case
				//This fixes sound in at least 1 game (Little Red Hood)
				_sweepTargetPeriod--;
			}
		} else {
			_sweepTargetPeriod = _period + shiftResult;
		}
		if(setPeriod && _sweepShift > 0 && _period >= 8 && _sweepTargetPeriod <= 0x7FF) {
			_period = _sweepTargetPeriod;
		}
	}

protected:
	void Clock()
	{
		_dutyPos = (_dutyPos - 1) & 0x07;

		if(IsMuted()) {
			AddOutput(0);
		} else {
			AddOutput(_dutySequences[_duty][_dutyPos] * GetVolume());
		}
	}

public:
	SquareChannel(AudioChannel channel, Blip_Buffer *buffer, bool isChannel1) : ApuEnvelope(channel, buffer)
	{
		SetVolume(0.1128);
		_isChannel1 = isChannel1;
	}

	virtual void Reset(bool softReset)
	{
		ApuEnvelope::Reset(softReset);
		
		_duty = 0;
		_dutyPos = 0;
	
		_sweepEnabled = false;
		_sweepPeriod = 0;
		_sweepNegate = false;
		_sweepShift = 0;
		_reloadSweep = false;
		_sweepDivider = 0;
		_sweepTargetPeriod = 0;
	}

	virtual void StreamState(bool saving)
	{
		ApuEnvelope::StreamState(saving);

		Stream<uint8_t>(_duty);
		Stream<uint8_t>(_dutyPos);
		Stream<bool>(_sweepEnabled);
		Stream<uint8_t>(_sweepPeriod);
		Stream<bool>(_sweepNegate);
		Stream<uint8_t>(_sweepShift);
		Stream<bool>(_reloadSweep);
		Stream<uint8_t>(_sweepDivider);
		Stream<uint32_t>(_sweepTargetPeriod);
	}

	void GetMemoryRanges(MemoryRanges &ranges)
	{
		if(_isChannel1) {
			ranges.AddHandler(MemoryOperation::Write, 0x4000, 0x4003);
		} else {
			ranges.AddHandler(MemoryOperation::Write, 0x4004, 0x4007);
		}
	}

	void WriteRAM(uint16_t addr, uint8_t value)
	{
		APU::StaticRun();
		switch(addr & 0x03) {
			case 0:		//4000 & 4004
				InitializeLengthCounter((value & 0x20) == 0x20);
				InitializeEnvelope(value);

				_duty = (value & 0xC0) >> 6;
				break;

			case 1:		//4001 & 4005
				InitializeSweep(value);
				break;

			case 2:		//4002 & 4006
				_period &= ~0x00FF;
				_period |= value;
				break;

			case 3:		//4003 & 4007
				LoadLengthCounter(value >> 3);

				_period &= ~0x0700;
				_period |= (value & 0x07) << 8;

				//The sequencer is restarted at the first value of the current sequence.
				_timer = _period + 1;
				_dutyPos = 0;

				//The envelope is also restarted.
				ResetEnvelope();
				break;
		}
	}

	void TickSweep()
	{
		if(_reloadSweep) {
			if(_sweepDivider == 0 && _sweepEnabled) {
				//If the divider's counter was zero before the reload and the sweep is enabled, the pulse's period is also adjusted
				UpdateTargetPeriod(true);
			}
			_sweepDivider = _sweepPeriod;
			_reloadSweep = false;
		} else {
			if(_sweepDivider > 0) {
				_sweepDivider--;
			} else if(_sweepEnabled) {
				UpdateTargetPeriod(true);
				_sweepDivider = _sweepPeriod;
			}
		}
	}

	void Run(uint32_t targetCycle)
	{
		UpdateTargetPeriod(false);
		ApuLengthCounter::Run(targetCycle);
	}
};