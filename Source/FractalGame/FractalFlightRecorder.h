// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FractalParameter.h"
#include "FractalSceneViewExtension.h"
#include "FractalMath/FractalScalar.h"

/** Everything needed to see a spot again exactly, and what the renderer did there. */
struct FFractalFlightState
{
	EFractalType FractalType = EFractalType::Mandelbulb;
	float Power = 8.0f;
	double UserScale = 1.0e-3;              // fractal units per cm before any zoom
	double Scale = 1.0e-3;                  // fractal units per cm now
	FractalMath::FDDVec3 Position;          // camera position in fractal space (double-double, restored losslessly)
	FQuat Rotation = FQuat::Identity;       // pawn (= camera) rotation; world axes are fractal axes
	float SpeedPercent = 50.0f;
	double DistanceCm = 0.0;                // camera to surface at the unzoomed scale
	double SpeedCmPerSecond = 0.0;          // at the unzoomed scale
	float ResolutionScale = 1.0f;
	float TargetFrameRate = 0.0f;
	FFractalRenderStats Render;
};

/**
 * Flight log for finding slow or broken views. While recording it appends a row to flight.csv about four times a
 * second (frame rate, worst frame, GPU time, iteration limit, reference rebuilds, position, rotation, zoom), and
 * every mark appends a row to marks.csv with a screenshot next to it. Both files live in
 * Saved/FlightLogs/<session start>/ and are flushed after every row, so they survive a crash.
 * Positions are written as exact double-double parts (%.17g) and as decimals for reading; LoadMark parses the
 * exact parts back, so AFractalPlayerController can return to a mark bit for bit.
 */
class FFractalFlightRecorder
{
public:
	FFractalFlightRecorder() = default;
	FFractalFlightRecorder(const FFractalFlightRecorder&) = delete;
	FFractalFlightRecorder& operator=(const FFractalFlightRecorder&) = delete;
	~FFractalFlightRecorder();

	void SetRecording(bool bInRecording);
	bool IsRecording() const { return bRecording; }

	/** Once per frame: collects frame times and writes a flight.csv row every SampleInterval seconds. */
	void Tick(float DeltaSeconds, const FFractalFlightState& State);

	/** Appends a marks.csv row and requests a screenshot; returns the mark number (1, 2, ...). */
	int32 AddMark(const FFractalFlightState& State, const FString& Note);

	int32 GetMarkCount() const { return MarkCount; }
	const FString& GetSessionDirectory() const { return SessionDirectory; }

	/** Root of all sessions (Saved/FlightLogs). */
	static FString GetLogRoot();

	/**
	 * Finds mark number MarkId in the session folder Session (a folder name under GetLogRoot(); empty = InCurrentSession
	 * if it has marks, else the newest session that has any). OutSource names the file it came from.
	 */
	static bool LoadMark(int32 MarkId, const FString& Session, const FString& InCurrentSession, FFractalFlightState& OutState, FString& OutNote, FString& OutSource);

	/** One line per mark of a session (same session rules as LoadMark). */
	static TArray<FString> ListMarks(const FString& Session, const FString& InCurrentSession, FString& OutSource);

	static constexpr double SampleInterval = 0.25;

private:
	struct FFrameSample
	{
		double Time;
		float FrameMilliseconds;
		float GpuMilliseconds;
	};

	/** Frame statistics over the samples newer than Since (seconds since the recording started). */
	struct FFrameSummary
	{
		int32 Frames = 0;
		double FramesPerSecond = 0.0;
		double FrameAverage = 0.0;
		double FrameWorst = 0.0;
		double GpuAverage = 0.0;
		double GpuWorst = 0.0;
	};
	FFrameSummary Summarize(double Since) const;

	bool EnsureSession();
	void WriteLine(FArchive* Ar, const FString& Line) const;
	FString FormatRow(const FFractalFlightState& State, const FFrameSummary& Frames) const;
	static FString Header();

	bool bRecording = true;
	FString SessionDirectory;
	FArchive* FlightFile = nullptr;
	FArchive* MarksFile = nullptr;
	int32 MarkCount = 0;

	double Clock = 0.0;            // seconds of recording
	double LastSampleTime = 0.0;
	TArray<FFrameSample> Frames;   // the last second of frames
	uint64 LastReferenceVersion = 0;
	int32 RebuildsSinceSample = 0;
	int32 MarkSinceSample = 0;     // mark taken since the last flight.csv row (0 = none)
};
