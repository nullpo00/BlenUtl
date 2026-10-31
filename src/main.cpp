//----------------------------------------------------------------------------------
// Blenderトランスフォームインポートプラグイン
//----------------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN		// Windows ヘッダーからほとんど使用されていない部分を除外する
#define NOMINMAX				// Windows ヘッダーのmin/maxマクロを無効化する
#include <Windows.h>
#include <shobjidl.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <Eigen/Core>
#include <Eigen/Geometry>

#include "plugin2.h"
#include "config2.h"
#include "logger2.h"
#include "io_config.h"
#include "resource.h"

#ifdef _DEBUG
#include <cstdio>
#include <format>
#include <iostream>
FILE* fp;
#endif

namespace {

constexpr const char* CONFIG_PATH = "Plugin/BlenUtl/BlenUtl.ini";
EDIT_HANDLE* edit_handle;
CONFIG_HANDLE* config;
LOG_HANDLE* logger;

// インポート設定データ構造体定義
struct ConfigData {
	int target_layer;				// 追加レイヤー
	int target_frame;				// 追加フレーム
	int scaling;					// スケーリング係数
	int zoom_method;				// 拡大率のインポート方法
	bool use_fov;					// 視野角をインポートするか？
	bool new_scene;					// 新規シーンを作成するか？
	bool use_resolution;			// 解像度をインポートするか？
	bool use_framerate;				// フレームレートをインポートするか？
	wchar_t const* const* items;	// コンボボックスアイテム
};

// Blenderデータ構造体定義
struct BLMatrix {
	Eigen::Vector3f location;		// 位置
	Eigen::Quaternionf rotation;	// 回転		
	Eigen::Vector3f scale;			// スケール
};

// インポートデータ構造体定義
struct ImportData {
	int width;						// 解像度 幅
	int height;						// 解像度 高さ
	int fps;						// フレームレート
	std::string obj_type;			// オブジェクトタイプ カメラかオブジェクトか
	int duration;					// 全体のフレームの長さ
	std::optional<float> fov;		// 視野角
	std::vector<BLMatrix> frames;	// Blenderフレーム情報
	const ConfigData* cfg_data;		// インポート設定
};

// 汎用プラグイン構造体定義
struct COMMON_PLUGIN_TABLE common_plugin_table = {
	L"BlenUtl",						// プラグインの名前
	L"BlenUtl for ExEdit2 v1.0.0"	// プラグインの情報
};

// 関数プロトタイプ
void func_proc_import(void* param);
void func_proc_edit(void* param, EDIT_SECTION* edit);
void func_config(HWND hwnd, HINSTANCE dll_hinst);
void from_json(const nlohmann::json& json, BLMatrix& data);
std::optional<std::wstring> open_file_dialog(HWND hwnd = nullptr);
INT_PTR CALLBACK dialog_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

//---------------------------------------------------------------------
// インポートメニュー選択時のコールバック関数
//---------------------------------------------------------------------
void func_proc_import([[maybe_unused]] void* param) {

	if (!edit_handle) { return; }
	HWND hwnd = edit_handle->get_host_app_window();
	const auto filepath = open_file_dialog(hwnd);
	if (!filepath) { return; }

	// Initialize
	ConfigData cfg_data{
		.target_layer = 0,
		.target_frame = 0,
		.scaling = 100,
		.zoom_method = 0,
		.use_fov = true,
		.new_scene = true,
		.use_resolution = true,
		.use_framerate = true,
		.items = nullptr
	};
	ImportData proc_data{
		.width = 1920,
		.height = 1080,
		.fps = 60,
		.obj_type = "",
		.duration = 0,
		.fov = std::nullopt,
		.frames = {},
		.cfg_data = &cfg_data
	};

	try {
		// 設定データ読み込み
		cfg::Config cfg{};
		const auto cfg_path = std::filesystem::path(config->app_data_path) / CONFIG_PATH;
		if (cfg::read_config(cfg_path, cfg)) {
			const auto& settings = cfg.at("settings");
			cfg_data = {
				.target_layer = std::stoi(settings.at("target_layer")),
				.target_frame = std::stoi(settings.at("target_frame")),
				.scaling = std::stoi(settings.at("scaling")),
				.zoom_method = std::stoi(settings.at("zoom_method")),
				.use_fov = static_cast<bool>(std::stoi(settings.at("use_fov"))),
				.new_scene = static_cast<bool>(std::stoi(settings.at("new_scene"))),
				.use_resolution = static_cast<bool>(std::stoi(settings.at("use_resolution"))),
				.use_framerate = static_cast<bool>(std::stoi(settings.at("use_framerate"))),
				.items = nullptr
			};
		}

		// JSON パース
		const auto path = std::filesystem::path(*filepath);
		std::ifstream file(path);
		if (file.is_open()) {
			nlohmann::json data = nlohmann::json::parse(file);

			proc_data.width = data.at("width").get<int>();
			proc_data.height = data.at("height").get<int>();
			proc_data.fps = data.at("fps").get<int>();
			proc_data.obj_type = data.at("object_type").get<std::string>();
			proc_data.duration = data.at("duration").get<int>();
			proc_data.fov = data.at("fov").is_null() ? std::nullopt : std::optional(data.at("fov").get<float>());
			proc_data.frames = data.at("frames").get<std::vector<BLMatrix>>();
		}

	} catch (const std::out_of_range&) {
		MessageBoxW(hwnd, L"設定ファイルの読み込みに失敗しました", L"エラー", MB_OK | MB_ICONEXCLAMATION);
		logger->error(logger, L"Key Error");
		return;
	} catch (const std::invalid_argument&) {
		MessageBoxW(hwnd, L"設定ファイルの読み込みに失敗しました", L"エラー", MB_OK | MB_ICONEXCLAMATION);
		logger->error(logger, L"Invalid Parameter");
		return;
	} catch (const nlohmann::json::exception&) {
		MessageBoxW(hwnd, L"データの変換に失敗しました", L"エラー", MB_OK | MB_ICONEXCLAMATION);
		logger->error(logger, L"Parse Error");
		return;
	}

	if (proc_data.duration <= 0) {
		return;
	} else if (cfg_data.new_scene) {
		// 新規シーン作成
		constexpr EDIT_INFO::COLOR background = { 0, 0, 0, 0 };
		if (!edit_handle->create_scene(L"ImportBlender", nullptr, proc_data.width, proc_data.height, proc_data.fps, 1, 44100, background)) {
			logger->error(logger, L"Failed to create a new scene");
			return;
		}
	}

	// インポート処理関数呼び出し
	if (!edit_handle->call_edit_section_param(static_cast<void*>(&proc_data), &func_proc_edit)) {
		MessageBoxW(hwnd, L"インポート処理に失敗しました", L"エラー", MB_OK | MB_ICONEXCLAMATION);
		logger->error(logger, L"Failed to invoke a callback function");
		return;
	}
}

//---------------------------------------------------------------------
// インポート処理関数
//---------------------------------------------------------------------
void func_proc_edit(void* param, EDIT_SECTION* edit) {

	if (!param || !edit || !edit_handle) { return; }
	auto* proc_data = static_cast<ImportData*>(param);
	const auto* cfg_data = proc_data->cfg_data;
	HWND hwnd = edit_handle->get_host_app_window();

	if (cfg_data->use_resolution) {
		edit->set_scene_size(proc_data->width, proc_data->height);
	}
	if (cfg_data->use_framerate) {
		edit->set_scene_frame_rate(proc_data->fps, 1);
	}

	Eigen::Vector3f target_vec{};	// カメラ正面方向ベクトル(Blender基準)
	Eigen::Vector3f up_vec{};		// カメラ上方向単位ベクトル(Blender基準)
	Eigen::Matrix3f basis{};		// Blender -> AviUtl2への基底変換行列
	basis << 1.0, 0.0, 0.0,		// AviUtl X -> Blender X
			 0.0, 0.0, -1.0,	// AviUtl Y -> Blender -Z
			 0.0, 1.0, 0.0;		// AviUtl Z -> Blender Y

	// AviUtl parameters
	struct vec_str { std::string x, y, z; };
	OBJECT_HANDLE obj = nullptr;		// 作成するオブジェクト
	vec_str loc = { "", "", "" };		// 位置
	vec_str target = { "", "", "" };	// 目標点
	vec_str up = { "", "", "" };		// カメラ上方向単位ベクトル
	vec_str rot = { "", "", "" };		// 回転
	std::string zoom = "";				// 拡大率

	const size_t cap = 64 * proc_data->duration;
	loc.x.reserve(cap);
	loc.y.reserve(cap);
	loc.z.reserve(cap);
	target.x.reserve(cap);
	target.y.reserve(cap);
	target.z.reserve(cap);
	up.x.reserve(cap);
	up.y.reserve(cap);
	up.z.reserve(cap);
	rot.x.reserve(cap);
	rot.y.reserve(cap);
	rot.z.reserve(cap);
	zoom.reserve(cap);

	std::wstring obj_name = proc_data->obj_type == "CAMERA" ? L"カメラ制御" : L"グループ制御";

	if ((obj = edit->create_object(obj_name.c_str(), cfg_data->target_layer, cfg_data->target_frame, proc_data->duration + 1))) {

		int cnt = cfg_data->target_frame + 1;
		// 座標系変換
		for (auto& frame : proc_data->frames) {

			// Location
			frame.location = cfg_data->scaling * basis * frame.location;

			// Rotation
			frame.rotation.normalize();
			Eigen::Vector3f rot_vec = frame.rotation.toRotationMatrix().canonicalEulerAngles(0, 1, 2);
			// Camera Rotation
			if (proc_data->obj_type == "CAMERA") {
				target_vec << 0.0, 1024.0, 0.0;
				up_vec << 0.0, 0.0, 1.0;
				target_vec = frame.rotation * target_vec;
				target_vec += frame.location;
				up_vec = frame.rotation * up_vec;
				up_vec.normalize();
			}

			// Scale
			float scale = 100.0f;
			frame.scale = (basis * frame.scale).cwiseAbs();
			switch (cfg_data->zoom_method) {
			case 0:
				scale *= frame.scale(0);
				break;
			case 1:
				scale *= frame.scale(1);
				break;
			case 2:
				scale *= frame.scale(2);
				break;
			case 3:
				scale *= frame.scale.sum() / 3.0;
				break;
			}

			// 丸め処理
			auto round = [](float x) {return std::round(x * 100.0f) / 100.0f; };
			std::ranges::transform(frame.location, frame.location.begin(), round);
			std::ranges::transform(target_vec, target_vec.begin(), round);
			std::ranges::transform(frame.scale, frame.scale.begin(), round);
			std::ranges::transform(rot_vec, rot_vec.begin(),
				[&round](float r) {return round(r * 180.0 / std::numbers::pi); });
			std::ranges::transform(up_vec, up_vec.begin(), round);
			scale = round(scale);

			// パラメーター追加
			loc.x += std::to_string(frame.location(0)) + ",";
			loc.y += std::to_string(frame.location(1)) + ",";
			loc.z += std::to_string(frame.location(2)) + ",";
			target.x += std::to_string(target_vec(0)) + ",";
			target.y += std::to_string(target_vec(1)) + ",";
			target.z += std::to_string(target_vec(2)) + ",";
			up.x += std::to_string(up_vec(0)) + ",";
			up.y += std::to_string(up_vec(1)) + ",";
			up.z += std::to_string(up_vec(2)) + ",";
			rot.x += std::to_string(rot_vec(0)) + ",";
			rot.y += std::to_string(rot_vec(1)) + ",";
			rot.z += std::to_string(rot_vec(2)) + ",";
			zoom += std::to_string(scale) + ",";

			// デバッグログ
			#ifdef _DEBUG
			std::cout << "frame:" << cnt - 1 << "\n";
			std::cout << "\tlocation:"
				<< std::format("[x:{}, y:{}, z:{}]\n", frame.location(0), frame.location(1), frame.location(2));
			std::cout << "\troatation:"
				<< std::format("[x:{}, y:{}, z:{}]\n", rot_vec(0), rot_vec(1), rot_vec(2));
			std::cout << "\tquaternion:"
				<< std::format("[w:{}, x:{}, y:{}, z:{}]\n", frame.rotation.w(), frame.rotation.x(), frame.rotation.y(), frame.rotation.z());
			std::cout << "\tscale:" << scale << "\n";
			std::cout << std::endl;
			#endif

			// 中間点追加
			edit->create_object_section(obj, cnt++);

		}

		// 移動モード
		loc.x += "直線移動,0";
		loc.y += "直線移動,0";
		loc.z += "直線移動,0";
		target.x += "直線移動,0";
		target.y += "直線移動,0";
		target.z += "直線移動,0";
		up.x += "直線移動,0";
		up.y += "直線移動,0";
		up.z += "直線移動,0";
		rot.x += "直線移動,0";
		rot.y += "直線移動,0";
		rot.z += "直線移動,0";
		zoom += "直線移動,0";

		// AviUtlオブジェクトへ設定値追加
		edit->set_object_item_value(obj, obj_name.c_str(), L"X", loc.x.c_str());
		edit->set_object_item_value(obj, obj_name.c_str(), L"Y", loc.y.c_str());
		edit->set_object_item_value(obj, obj_name.c_str(), L"Z", loc.z.c_str());
		if (proc_data->obj_type == "CAMERA") {
			auto effect = edit->create_effect(obj, L"CameraUp@BlenUtl");
			edit->set_object_item_value(obj, obj_name.c_str(), L"目標X", target.x.c_str());
			edit->set_object_item_value(obj, obj_name.c_str(), L"目標Y", target.y.c_str());
			edit->set_object_item_value(obj, obj_name.c_str(), L"目標Z", target.z.c_str());
			if (effect) {
				edit->set_effect_item_value(effect, L"X", up.x.c_str());
				edit->set_effect_item_value(effect, L"Y", up.y.c_str());
				edit->set_effect_item_value(effect, L"Z", up.z.c_str());
			}
			if (cfg_data->use_fov && proc_data->fov) {
				edit->set_object_item_value(obj, obj_name.c_str(), L"視野角", std::to_string(*(proc_data->fov)).c_str());
			}
		} else {
			edit->set_object_item_value(obj, obj_name.c_str(), L"X軸回転", rot.x.c_str());
			edit->set_object_item_value(obj, obj_name.c_str(), L"Y軸回転", rot.y.c_str());
			edit->set_object_item_value(obj, obj_name.c_str(), L"Z軸回転", rot.z.c_str());
			edit->set_object_item_value(obj, obj_name.c_str(), L"拡大率", zoom.c_str());
		}

	} else {
		MessageBoxW(hwnd, L"オブジェクト生成に失敗しました", L"エラー", MB_OK | MB_ICONEXCLAMATION);
		logger->error(logger, L"Failed to create object");
		return;
	}
}

//---------------------------------------------------------------------
// 設定メニュー選択時のコールバック関数
//---------------------------------------------------------------------
void func_config(HWND hwnd, HINSTANCE dll_hinst) {

	if (!edit_handle) { return; }

	// Initialize
	constexpr wchar_t const* items[] = { L"X", L"Y", L"Z", L"平均", nullptr };
	ConfigData data = {
		.target_layer = 0,
		.target_frame = 0,
		.scaling = 100,
		.zoom_method = 0,
		.use_fov = true,
		.new_scene = true,
		.use_resolution = true,
		.use_framerate = true,
		.items = items
	};

	if (config) {
		const auto path = std::filesystem::path(config->app_data_path) / CONFIG_PATH;
		cfg::Config init_data = {};
		// 設定値読み込み
		if (cfg::read_config(path, init_data)) {
			const auto& settings = init_data.at("settings");
			data = {
				.target_layer = std::stoi(settings.at("target_layer")),
				.target_frame = std::stoi(settings.at("target_frame")),
				.scaling = std::stoi(settings.at("scaling")),
				.zoom_method = std::stoi(settings.at("zoom_method")),
				.use_fov = static_cast<bool>(std::stoi(settings.at("use_fov"))),
				.new_scene = static_cast<bool>(std::stoi(settings.at("new_scene"))),
				.use_resolution = static_cast<bool>(std::stoi(settings.at("use_resolution"))),
				.use_framerate = static_cast<bool>(std::stoi(settings.at("use_framerate"))),
				.items = items
			};
		}

		const auto result = DialogBoxParam(
			dll_hinst ? dll_hinst : GetModuleHandle(0),
			MAKEINTRESOURCE(SETTING_DIALOG),
			hwnd ? hwnd : nullptr,
			&dialog_proc,
			reinterpret_cast<LPARAM>(&data));

		if (result == IDOK) {
			const cfg::Config cfg{
				{"settings",
					{
						{"target_layer", std::to_string(data.target_layer)},
						{"target_frame", std::to_string(data.target_frame)},
						{"scaling", std::to_string(data.scaling)},
						{"zoom_method", data.zoom_method != CB_ERR ? std::to_string(data.zoom_method) : "0"},
						{"use_fov", data.use_fov ? "1" : "0"},
						{"new_scene", data.new_scene ? "1" : "0"},
						{"use_resolution", data.use_resolution ? "1" : "0"},
						{"use_framerate", data.use_framerate ? "1" : "0"}
					}
				}
			};
			if (!cfg::write_config(path, cfg)) {
				logger->error(logger, L"Failed to write config");
			}
		}
	}
}

//---------------------------------------------------------------------
// ダイアログプロシージャ
//---------------------------------------------------------------------
INT_PTR CALLBACK dialog_proc(HWND hdlg, UINT message, WPARAM wparam, LPARAM lparam) {
	switch (message) {
	case WM_INITDIALOG: {
		SetWindowLongPtr(hdlg, DWLP_USER, static_cast<LONG_PTR>(lparam));
		const auto* data = reinterpret_cast<ConfigData*>(GetWindowLongPtr(hdlg, DWLP_USER));
		if (data) {
			try {
				SetDlgItemInt(hdlg, IDC_TARGET_LAYER, data->target_layer, false);
				SetDlgItemInt(hdlg, IDC_TARGET_FRAME, data->target_frame, false);
				SetDlgItemInt(hdlg, IDC_SCALING, data->scaling, false);
				CheckDlgButton(hdlg, IDC_USE_FOV, data->use_fov ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hdlg, IDC_NEW_SCENE, data->new_scene ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hdlg, IDC_USE_RESOLUTION, data->use_resolution ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hdlg, IDC_USE_FRAMERATE, data->use_framerate ? BST_CHECKED : BST_UNCHECKED);

				for (const auto* item = data->items; *item; item++) {
					SendMessageW(GetDlgItem(hdlg, IDC_ZOOM_METHOD), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(*item));
				}
				SendMessageW(GetDlgItem(hdlg, IDC_ZOOM_METHOD), CB_SETCURSEL, data->zoom_method, 0);

				SendMessageW(GetDlgItem(hdlg, IDC_LAYER_SPIN), UDM_SETRANGE, 0, MAKELONG(100, 0));
				SendMessageW(GetDlgItem(hdlg, IDC_FRAME_SPIN), UDM_SETRANGE, 0, MAKELONG(10000, 0));
				SendMessageW(GetDlgItem(hdlg, IDC_SCALING_SPIN), UDM_SETRANGE, 0, MAKELONG(1000, 0));

			} catch (const std::out_of_range&) {
				MessageBoxW(edit_handle->get_host_app_window(), L"設定に必要なキーが存在しません", L"エラー", MB_OK | MB_ICONEXCLAMATION);
				logger->error(logger, L"Key Error");
				EndDialog(hdlg, LOWORD(wparam));
				return true;
			}
		}
		return true;
	}
	case WM_COMMAND:
		switch (LOWORD(wparam)) {
		case IDOK: {
			auto* data = reinterpret_cast<ConfigData*>(GetWindowLongPtr(hdlg, DWLP_USER));
			int result = 0, ret = 0;
			constexpr int ids[] = {
				IDC_TARGET_LAYER,
				IDC_TARGET_FRAME,
				IDC_SCALING
			};
			int int_item[] = { 0, 0, 100 };

			for (int i = 0; i < _countof(int_item); i++) {
				ret = static_cast<int>(GetDlgItemInt(hdlg, ids[i], &result, true));
				if (result) {
					int_item[i] = std::abs(ret);
				}
			}
			data->target_layer = int_item[0];
			data->target_frame = int_item[1];
			data->scaling = int_item[2];
			data->zoom_method = SendMessageW(GetDlgItem(hdlg, IDC_ZOOM_METHOD), CB_GETCURSEL, 0, 0);
			data->use_fov = SendMessageW(GetDlgItem(hdlg, IDC_USE_FOV), BM_GETCHECK, 0, 0) == BST_CHECKED ? true : false;
			data->new_scene = SendMessageW(GetDlgItem(hdlg, IDC_NEW_SCENE), BM_GETCHECK, 0, 0) == BST_CHECKED ? true : false;
			data->use_resolution = SendMessageW(GetDlgItem(hdlg, IDC_USE_RESOLUTION), BM_GETCHECK, 0, 0) == BST_CHECKED ? true : false;
			data->use_framerate = SendMessageW(GetDlgItem(hdlg, IDC_USE_FRAMERATE), BM_GETCHECK, 0, 0) == BST_CHECKED ? true : false;

			EndDialog(hdlg, LOWORD(wparam));
			return true;
		}
		case IDCANCEL: {
			EndDialog(hdlg, LOWORD(wparam));
			return true;
		}
		}
		return false;
	default:
		return false;
	}
}

//---------------------------------------------------------------------
// JSONデータマッピング関数
//---------------------------------------------------------------------
void from_json(const nlohmann::json& json, BLMatrix& data) {

	const auto& transform = json.at("transform");
	// 各ベクトルに分解する: 
	// [[loc_x, loc_y, loc_z], [rot_w, rot_x, rot_y, rot_z], [scl_x, scl_y, scl_z]]
	for (int i = 0; i < data.location.size(); i++) {
		data.location(i) = transform.at(0).at(i).get<float>();
		data.scale(i) = transform.at(2).at(i).get<float>();
	}

	// Quaternion
	const auto& quat = transform.at(1);
	data.rotation.w() = quat.at(0).get<float>();
	data.rotation.x() = quat.at(1).get<float>();
	data.rotation.y() = -quat.at(3).get<float>();	// 正規直行基底なのでそのまま入れ替える
	data.rotation.z() = quat.at(2).get<float>();

}

//---------------------------------------------------------------------
// ファイル選択ダイアログ
//---------------------------------------------------------------------
std::optional<std::wstring> open_file_dialog(HWND hwnd) {

	std::optional<std::wstring> result = std::nullopt;	// 選択ファイルパス 失敗時はnullopt
	HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

	if (SUCCEEDED(hr)) {

		IFileOpenDialog* dialog = nullptr;
		hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
			IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));

		if (SUCCEEDED(hr) && dialog) {
			// ファイルフィルター
			COMDLG_FILTERSPEC rg_filter_spec[] = {
				{L"JSONファイル", L"*.json"}
			};
			dialog->SetFileTypes(ARRAYSIZE(rg_filter_spec), rg_filter_spec);
			hr = dialog->Show(hwnd);

			if (SUCCEEDED(hr)) {
				IShellItem* item = nullptr;
				hr = dialog->GetResult(&item);

				if (SUCCEEDED(hr) && item) {
					PWSTR psz_file_path = nullptr;
					hr = item->GetDisplayName(SIGDN_FILESYSPATH, &psz_file_path);

					if (SUCCEEDED(hr) && psz_file_path) {
						// ファイルパス取得
						result = psz_file_path;
						CoTaskMemFree(psz_file_path);
					}
					item->Release();
				}
			}
			dialog->Release();
		}
		CoUninitialize();
	}
	return result;
}

}	// namespace

//---------------------------------------------------------------------
// 必要とする本体バージョン番号取得関数
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
	return 2010110;
}

//---------------------------------------------------------------------
// 設定関連機能初期化関数
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* config_handle) {
	config = config_handle;
}

//---------------------------------------------------------------------
// ログ出力機能初期化関数
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* log_handle) {
	logger = log_handle;
}

//---------------------------------------------------------------------
// プラグインDLL初期化関数
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) bool InitializePlugin([[maybe_unused]] DWORD version) {

#ifdef _DEBUG
	AllocConsole();
	freopen_s(&fp, "CONOUT$", "w", stdout);
#endif

	if (config) {
		const auto path = std::filesystem::path(config->app_data_path) / CONFIG_PATH;

		if (!std::filesystem::exists(path)) {
			// Initialize config
			const cfg::Config cfg{
				{"settings",
					{
						{"target_layer", "0"},
						{"target_frame", "0"},
						{"scaling", "100"},
						{"zoom_method", "0"},
						{"use_fov", "1"},
						{"new_scene", "1"},
						{"use_resolution", "1"},
						{"use_framerate", "1"}
					}
				}
			};
			return cfg::write_config(path, cfg);
		}
	}
	return true;
}

//---------------------------------------------------------------------
//	プラグインDLL解放関数
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void UninitializePlugin() {
#ifdef _DEBUG
	if (fp) {
		fclose(fp);
	}
	FreeConsole();
#endif
}

//---------------------------------------------------------------------
// 汎用プラグイン構造体のポインタを渡す関数
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) {
	return &common_plugin_table;
}

//---------------------------------------------------------------------
// プラグイン登録関数
//---------------------------------------------------------------------
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
	if (host) {
		edit_handle = host->create_edit_handle();
		host->register_import_menu_param(L"Blender (.json)", nullptr, &func_proc_import);
		host->register_config_menu(L"BlenUtl", &func_config);
	}
}