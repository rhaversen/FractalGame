// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractalFlightRecorder.h"
#include "FractalControlSubsystem.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogFractalFlight, Log, All);

namespace
{
	const TCHAR* const FlightFileName = TEXT("flight.csv");
	const TCHAR* const MarksFileName = TEXT("marks.csv");
	const TCHAR* const MarkColumns = TEXT("mark,wall_time,screenshot,note,");

	// Shortest text that reads back as exactly the same double.
	FString Exact(double Value)
	{
		return FString::Printf(TEXT("%.17g"), Value);
	}

	// Decimals that still change when the camera moves a fraction of a centimetre (as on the HUD).
	int32 CoordinateDecimals(double Scale)
	{
		return FMath::Clamp(FMath::CeilToInt32(-FMath::Loge(Scale) / FMath::Loge(10.0)) + 2, 4, 30);
	}

	// Notes are free text inside a comma-separated row.
	FString SanitizeNote(const FString& Note)
	{
		FString Result = Note.Replace(TEXT(","), TEXT(";")).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\""), TEXT("'"));
		Result.TrimStartAndEndInline();
		return Result;
	}

	FString ResolveSessionDirectory(const FString& Session, const FString& CurrentSession)
	{
		const FString Root = FFractalFlightRecorder::GetLogRoot();
		if (!Session.IsEmpty())
		{
			return FPaths::IsRelative(Session) ? FPaths::Combine(Root, Session) : Session;
		}
		if (!CurrentSession.IsEmpty() && FPaths::FileExists(FPaths::Combine(CurrentSession, MarksFileName)))
		{
			return CurrentSession;
		}
		// Session folders are named by their start time, so the newest sorts last.
		TArray<FString> Sessions;
		IFileManager::Get().FindFiles(Sessions, *FPaths::Combine(Root, TEXT("*")), false, true);
		Sessions.Sort();
		for (int32 I = Sessions.Num() - 1; I >= 0; --I)
		{
			const FString Directory = FPaths::Combine(Root, Sessions[I]);
			if (FPaths::FileExists(FPaths::Combine(Directory, MarksFileName)))
			{
				return Directory;
			}
		}
		return FString();
	}

	struct FCsvTable
	{
		TMap<FString, int32> Columns;
		TArray<TArray<FString>> Rows;

		const FString* Get(const TArray<FString>& Row, const TCHAR* Name) const
		{
			const int32* Index = Columns.Find(Name);
			return (Index && Row.IsValidIndex(*Index)) ? &Row[*Index] : nullptr;
		}
		double GetDouble(const TArray<FString>& Row, const TCHAR* Name, double Default = 0.0) const
		{
			const FString* Value = Get(Row, Name);
			return Value ? FCString::Atod(**Value) : Default;
		}
	};

	bool LoadCsv(const FString& Path, FCsvTable& OutTable)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *Path) || Lines.Num() == 0)
		{
			return false;
		}
		TArray<FString> Header;
		Lines[0].ParseIntoArray(Header, TEXT(","), false);
		for (int32 I = 0; I < Header.Num(); ++I)
		{
			OutTable.Columns.Add(Header[I].TrimStartAndEnd(), I);
		}
		for (int32 I = 1; I < Lines.Num(); ++I)
		{
			if (!Lines[I].TrimStartAndEnd().IsEmpty())
			{
				TArray<FString>& Row = OutTable.Rows.AddDefaulted_GetRef();
				Lines[I].ParseIntoArray(Row, TEXT(","), false);
			}
		}
		return true;
	}
}

FFractalFlightRecorder::~FFractalFlightRecorder()
{
	delete FlightFile;
	delete MarksFile;
}

FString FFractalFlightRecorder::GetLogRoot()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("FlightLogs")));
}

void FFractalFlightRecorder::SetRecording(bool bInRecording)
{
	bRecording = bInRecording;
	// A pause must not count as one long frame.
	Frames.Reset();
	LastSampleTime = Clock;
	UE_LOG(LogFractalFlight, Log, TEXT("Flight recording %s"), bRecording ? TEXT("on") : TEXT("paused"));
}

bool FFractalFlightRecorder::EnsureSession()
{
	if (FlightFile)
	{
		return true;
	}
	if (SessionDirectory.IsEmpty())
	{
		SessionDirectory = FPaths::Combine(GetLogRoot(), FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H-%M-%S")));
		IFileManager::Get().MakeDirectory(*SessionDirectory, true);
		UE_LOG(LogFractalFlight, Log, TEXT("Flight log: %s"), *SessionDirectory);
	}
	FlightFile = IFileManager::Get().CreateFileWriter(*FPaths::Combine(SessionDirectory, FlightFileName), FILEWRITE_AllowRead);
	if (!FlightFile)
	{
		UE_LOG(LogFractalFlight, Warning, TEXT("Could not create %s in %s"), FlightFileName, *SessionDirectory);
		return false;
	}
	WriteLine(FlightFile, Header());
	return true;
}

void FFractalFlightRecorder::WriteLine(FArchive* Ar, const FString& Line) const
{
	if (!Ar)
	{
		return;
	}
	const FTCHARToUTF8 Utf8(*(Line + TEXT("\n")));
	Ar->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	Ar->Flush();
}

FString FFractalFlightRecorder::Header()
{
	return TEXT("time_s,fps,frame_ms_avg,frame_ms_worst,gpu_ms_avg,gpu_ms_worst,fractal_type,fractal,power,user_scale,scale,magnification,")
		TEXT("distance_cm,speed_cm_s,speed_percent,resolution_scale,target_fps,march_w,march_h,iterations,orbit_length,")
		TEXT("reference_version,reference_rebuilds,reference_ms,reference_hit,mark_taken,pos_x,pos_y,pos_z,")
		TEXT("pos_x_hi,pos_x_lo,pos_y_hi,pos_y_lo,pos_z_hi,pos_z_lo,quat_x,quat_y,quat_z,quat_w,pitch,yaw,roll");
}

FString FFractalFlightRecorder::FormatRow(const FFractalFlightState& State, const FFrameSummary& Summary) const
{
	const int32 Decimals = CoordinateDecimals(State.Scale);
	const FRotator Rotator = State.Rotation.Rotator();
	const FractalMath::FDDVec3& P = State.Position;
	const FFractalRenderStats& R = State.Render;

	TArray<FString> Fields;
	Fields.Reserve(42);
	Fields.Add(FString::Printf(TEXT("%.3f"), Clock));
	Fields.Add(FString::Printf(TEXT("%.1f"), Summary.FramesPerSecond));
	Fields.Add(FString::Printf(TEXT("%.2f"), Summary.FrameAverage));
	Fields.Add(FString::Printf(TEXT("%.2f"), Summary.FrameWorst));
	Fields.Add(FString::Printf(TEXT("%.2f"), Summary.GpuAverage));
	Fields.Add(FString::Printf(TEXT("%.2f"), Summary.GpuWorst));
	Fields.Add(FString::FromInt(static_cast<int32>(State.FractalType)));
	Fields.Add(UFractalControlSubsystem::GetFractalDisplayName(State.FractalType).Replace(TEXT(","), TEXT(" ")));
	Fields.Add(Exact(State.Power));
	Fields.Add(Exact(State.UserScale));
	Fields.Add(Exact(State.Scale));
	Fields.Add(FString::Printf(TEXT("%.6e"), State.UserScale / State.Scale));
	Fields.Add(FString::Printf(TEXT("%.6e"), State.DistanceCm));
	Fields.Add(FString::Printf(TEXT("%.6e"), State.SpeedCmPerSecond));
	Fields.Add(FString::Printf(TEXT("%.1f"), State.SpeedPercent));
	Fields.Add(FString::Printf(TEXT("%.3f"), State.ResolutionScale));
	Fields.Add(FString::Printf(TEXT("%.0f"), State.TargetFrameRate));
	Fields.Add(FString::FromInt(R.MarchExtent.X));
	Fields.Add(FString::FromInt(R.MarchExtent.Y));
	Fields.Add(FString::FromInt(R.Iterations));
	Fields.Add(FString::FromInt(R.OrbitLength));
	Fields.Add(FString::Printf(TEXT("%llu"), R.ReferenceVersion));
	Fields.Add(FString::FromInt(RebuildsSinceSample));
	Fields.Add(FString::Printf(TEXT("%.2f"), R.ReferenceMilliseconds));
	Fields.Add(R.bReferenceHit ? TEXT("1") : TEXT("0"));
	Fields.Add(FString::FromInt(MarkSinceSample)); // the screenshot of a mark costs a slow frame; this says why
	Fields.Add(UFractalControlSubsystem::FormatCoordinate(P.X, Decimals));
	Fields.Add(UFractalControlSubsystem::FormatCoordinate(P.Y, Decimals));
	Fields.Add(UFractalControlSubsystem::FormatCoordinate(P.Z, Decimals));
	Fields.Add(Exact(P.X.Hi));
	Fields.Add(Exact(P.X.Lo));
	Fields.Add(Exact(P.Y.Hi));
	Fields.Add(Exact(P.Y.Lo));
	Fields.Add(Exact(P.Z.Hi));
	Fields.Add(Exact(P.Z.Lo));
	Fields.Add(Exact(State.Rotation.X));
	Fields.Add(Exact(State.Rotation.Y));
	Fields.Add(Exact(State.Rotation.Z));
	Fields.Add(Exact(State.Rotation.W));
	Fields.Add(FString::Printf(TEXT("%.4f"), Rotator.Pitch));
	Fields.Add(FString::Printf(TEXT("%.4f"), Rotator.Yaw));
	Fields.Add(FString::Printf(TEXT("%.4f"), Rotator.Roll));
	return FString::Join(Fields, TEXT(","));
}

FFractalFlightRecorder::FFrameSummary FFractalFlightRecorder::Summarize(double Since) const
{
	FFrameSummary Summary;
	double FrameTotal = 0.0;
	double GpuTotal = 0.0;
	int32 GpuFrames = 0;
	for (const FFrameSample& Sample : Frames)
	{
		if (Sample.Time <= Since)
		{
			continue;
		}
		++Summary.Frames;
		FrameTotal += Sample.FrameMilliseconds;
		Summary.FrameWorst = FMath::Max(Summary.FrameWorst, static_cast<double>(Sample.FrameMilliseconds));
		if (Sample.GpuMilliseconds > 0.0f)
		{
			++GpuFrames;
			GpuTotal += Sample.GpuMilliseconds;
			Summary.GpuWorst = FMath::Max(Summary.GpuWorst, static_cast<double>(Sample.GpuMilliseconds));
		}
	}
	if (Summary.Frames > 0)
	{
		Summary.FrameAverage = FrameTotal / Summary.Frames;
		Summary.FramesPerSecond = FrameTotal > 0.0 ? 1000.0 * Summary.Frames / FrameTotal : 0.0;
	}
	if (GpuFrames > 0)
	{
		Summary.GpuAverage = GpuTotal / GpuFrames;
	}
	return Summary;
}

void FFractalFlightRecorder::Tick(float DeltaSeconds, const FFractalFlightState& State)
{
	if (State.Render.ReferenceVersion != LastReferenceVersion)
	{
		++RebuildsSinceSample;
		LastReferenceVersion = State.Render.ReferenceVersion;
	}
	if (!bRecording || !(DeltaSeconds > 0.0f))
	{
		return;
	}

	Clock += DeltaSeconds;
	// GPU time of the last completed frame (0 when the RHI does not report it).
	const uint32 GpuCycles = RHIGetGPUFrameCycles();
	Frames.Add({Clock, DeltaSeconds * 1000.0f, GpuCycles > 0 ? static_cast<float>(FPlatformTime::ToMilliseconds(GpuCycles)) : 0.0f});
	int32 Expired = 0;
	while (Expired < Frames.Num() && Frames[Expired].Time < Clock - 1.0)
	{
		++Expired;
	}
	Frames.RemoveAt(0, Expired, EAllowShrinking::No);

	if (Clock - LastSampleTime < SampleInterval || !EnsureSession())
	{
		return;
	}
	WriteLine(FlightFile, FormatRow(State, Summarize(LastSampleTime)));
	LastSampleTime = Clock;
	RebuildsSinceSample = 0;
	MarkSinceSample = 0;
}

int32 FFractalFlightRecorder::AddMark(const FFractalFlightState& State, const FString& Note)
{
	if (!EnsureSession())
	{
		return 0;
	}
	if (!MarksFile)
	{
		MarksFile = IFileManager::Get().CreateFileWriter(*FPaths::Combine(SessionDirectory, MarksFileName), FILEWRITE_AllowRead);
		if (!MarksFile)
		{
			UE_LOG(LogFractalFlight, Warning, TEXT("Could not create %s in %s"), MarksFileName, *SessionDirectory);
			return 0;
		}
		WriteLine(MarksFile, FString(MarkColumns) + Header());
	}

	const int32 Mark = ++MarkCount;
	MarkSinceSample = Mark;
	const FString Screenshot = FString::Printf(TEXT("mark_%03d.png"), Mark);
	// With the HUD, so the image shows the numbers as well.
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(SessionDirectory, Screenshot), true, false);

	// The mark covers the last second of frames, the time it takes to notice and press the key.
	const FString Prefix = FString::Printf(TEXT("%d,%s,%s,%s,"), Mark, *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")), *Screenshot, *SanitizeNote(Note));
	WriteLine(MarksFile, Prefix + FormatRow(State, Summarize(Clock - 1.0)));
	UE_LOG(LogFractalFlight, Log, TEXT("Mark %d saved to %s"), Mark, *SessionDirectory);
	return Mark;
}

bool FFractalFlightRecorder::LoadMark(int32 MarkId, const FString& Session, const FString& InCurrentSession, FFractalFlightState& OutState, FString& OutNote, FString& OutSource)
{
	const FString Directory = ResolveSessionDirectory(Session, InCurrentSession);
	OutSource = FPaths::Combine(Directory, MarksFileName);
	FCsvTable Table;
	if (Directory.IsEmpty() || !LoadCsv(OutSource, Table))
	{
		return false;
	}
	for (const TArray<FString>& Row : Table.Rows)
	{
		const FString* Id = Table.Get(Row, TEXT("mark"));
		if (!Id || FCString::Atoi(**Id) != MarkId)
		{
			continue;
		}
		const int32 Type = static_cast<int32>(Table.GetDouble(Row, TEXT("fractal_type")));
		if (Type < 0 || Type >= static_cast<int32>(EFractalType::Count))
		{
			return false;
		}
		OutState.FractalType = static_cast<EFractalType>(Type);
		OutState.Power = static_cast<float>(Table.GetDouble(Row, TEXT("power"), 8.0));
		OutState.UserScale = Table.GetDouble(Row, TEXT("user_scale"), 1.0e-3);
		OutState.Scale = Table.GetDouble(Row, TEXT("scale"), OutState.UserScale);
		OutState.SpeedPercent = static_cast<float>(Table.GetDouble(Row, TEXT("speed_percent"), 50.0));
		OutState.Position = FractalMath::FDDVec3(
			FractalMath::FDD(Table.GetDouble(Row, TEXT("pos_x_hi")), Table.GetDouble(Row, TEXT("pos_x_lo"))),
			FractalMath::FDD(Table.GetDouble(Row, TEXT("pos_y_hi")), Table.GetDouble(Row, TEXT("pos_y_lo"))),
			FractalMath::FDD(Table.GetDouble(Row, TEXT("pos_z_hi")), Table.GetDouble(Row, TEXT("pos_z_lo"))));
		OutState.Rotation = FQuat(Table.GetDouble(Row, TEXT("quat_x")), Table.GetDouble(Row, TEXT("quat_y")),
			Table.GetDouble(Row, TEXT("quat_z")), Table.GetDouble(Row, TEXT("quat_w"), 1.0)).GetNormalized();
		const FString* Note = Table.Get(Row, TEXT("note"));
		OutNote = Note ? *Note : FString();
		return true;
	}
	return false;
}

TArray<FString> FFractalFlightRecorder::ListMarks(const FString& Session, const FString& InCurrentSession, FString& OutSource)
{
	TArray<FString> Lines;
	const FString Directory = ResolveSessionDirectory(Session, InCurrentSession);
	OutSource = FPaths::Combine(Directory, MarksFileName);
	FCsvTable Table;
	if (Directory.IsEmpty() || !LoadCsv(OutSource, Table))
	{
		return Lines;
	}
	for (const TArray<FString>& Row : Table.Rows)
	{
		const FString* Id = Table.Get(Row, TEXT("mark"));
		const FString* Fractal = Table.Get(Row, TEXT("fractal"));
		const FString* Note = Table.Get(Row, TEXT("note"));
		Lines.Add(FString::Printf(TEXT("%s: %s  zoom x%.2e  %.0f fps (worst frame %.1f ms)  %d iterations  %s"),
			Id ? **Id : TEXT("?"), Fractal ? **Fractal : TEXT(""), Table.GetDouble(Row, TEXT("magnification")),
			Table.GetDouble(Row, TEXT("fps")), Table.GetDouble(Row, TEXT("frame_ms_worst")),
			static_cast<int32>(Table.GetDouble(Row, TEXT("iterations"))), Note ? **Note : TEXT("")));
	}
	return Lines;
}
