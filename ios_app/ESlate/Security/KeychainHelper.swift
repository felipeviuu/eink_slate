import Foundation
import Security

/// Minimal, zero-dependency helper for securely storing and retrieving the slate secret PIN
/// using Apple's Security framework (Keychain) with a transparent fallback to UserDefaults.
public enum KeychainHelper {
    private static let service = "com.nebralta.eslate"
    private static let account = "slateSecretPin"
    private static let fallbackKey = "com.nebralta.eslate.fallbackSecretPin"

    /// Saves the secret PIN securely in Keychain (or fallback storage).
    @discardableResult
    public static func save(pin: String) -> Bool {
        guard let data = pin.data(using: .utf8) else { return false }
        delete()

        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecValueData as String: data,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        ]

        let status = SecItemAdd(query as CFDictionary, nil)
        if status == errSecSuccess {
            UserDefaults.standard.removeObject(forKey: fallbackKey)
            return true
        } else {
            // Fallback for sandboxed or simulator environments without keychain entitlements
            UserDefaults.standard.set(pin, forKey: fallbackKey)
            return true
        }
    }

    /// Retrieves the secret PIN from Keychain, checking fallback if needed.
    public static func load() -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]

        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)
        if status == errSecSuccess,
           let data = item as? Data,
           let pin = String(data: data, encoding: .utf8),
           !pin.isEmpty {
            return pin
        }

        // Check fallback storage
        if let fallback = UserDefaults.standard.string(forKey: fallbackKey), !fallback.isEmpty {
            return fallback
        }

        return nil
    }

    /// Deletes the secret PIN from both Keychain and fallback storage.
    @discardableResult
    public static func delete() -> Bool {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account
        ]
        SecItemDelete(query as CFDictionary)
        UserDefaults.standard.removeObject(forKey: fallbackKey)
        return true
    }

    /// Returns whether a secret PIN is currently stored.
    public static func hasSavedPin() -> Bool {
        return load() != nil
    }
}
