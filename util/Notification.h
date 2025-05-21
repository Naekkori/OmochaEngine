#pragma once
#include <string>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <iostream> // 에러 로깅용

const std::wstring APP_AUMID = L"Naekkori.OmoChaEngine.DesktopApp";

class NotificationManager {
public:
    static void ShowUWPToast(const std::wstring& title, const std::wstring& message) {
        try {
            winrt::Windows::UI::Notifications::ToastNotifier notifier =
                winrt::Windows::UI::Notifications::ToastNotificationManager::CreateToastNotifier(APP_AUMID);

            if (!notifier) {
                std::wcerr << L"Error: Could not create ToastNotifier. Is AUMID (" << APP_AUMID << L") registered correctly and notifications enabled?" << std::endl;
                // 여기에 대체 알림(예: 로그 파일) 또는 오류 처리를 추가할 수 있습니다.
                return;
            }

            // 간단한 텍스트 토스트 XML 생성
            std::wstring toastXmlString = L"<toast launch='app-defined-string'>"
                                          L"<visual>"
                                          L"<binding template='ToastGeneric'>"
                                          L"<text>" + title + L"</text>"
                                          L"<text>" + message + L"</text>"
                                          // 앱 아이콘을 표시하려면 <image> 태그 추가 가능 (AUMID와 연관된 아이콘 또는 지정된 경로)
                                          // L"<image placement='appLogoOverride' src='file:///C:/path/to/your/icon.png'/>"
                                          L"</binding>"
                                          L"</visual>"
                                          // 오디오 추가 가능 (예: 기본 알림음)
                                          // L"<audio src='ms-winsoundevent:Notification.Default'/>"
                                          L"</toast>";

            winrt::Windows::Data::Xml::Dom::XmlDocument toastXml;
            toastXml.LoadXml(toastXmlString);

            winrt::Windows::UI::Notifications::ToastNotification toast(toastXml);

            // 토스트 만료 시간 설정 (선택 사항)
            // toast.ExpirationTime(winrt::Windows::Foundation::DateTime::clock::now() + std::chrono::seconds(30));

            notifier.Show(toast);
            std::wcout << L"UWP Toast notification shown: " << title << L" - " << message << std::endl;

        } catch (winrt::hresult_error const& ex) {
            std::wcerr << L"WinRT Error showing toast: " << ex.message().c_str() << L" (Code: " << std::hex << ex.code() << L")" << std::endl;
            std::wcerr << L"Ensure AUMID '" << APP_AUMID << L"' is correct and registered." << std::endl;
            std::wcerr << L"Also, check if notifications are enabled in Windows settings for your app." << std::endl;
        } catch (std::exception const& ex) {
            std::cerr << "Standard Exception showing toast: " << ex.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception showing toast." << std::endl;
        }
    }
};
